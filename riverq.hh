#pragma once

#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>
#include <atomic>
#include <cstring>
#include <stdexcept>
#include <x86intrin.h>

#define STOP_HAMMER_TIME2(x, y) x ## y
#define CONCATIFY(x, y) STOP_HAMMER_TIME2(x, y)

#define CACHE_LINE_SIZE 64u
#define FALSE_SHARING_PADDING_SIZE CACHE_LINE_SIZE*2

#define PADDING(x) \
        _Pragma("clang diagnostic push") \
        _Pragma("clang diagnostic ignored \"-Wunused-private-field\"") \
        char CONCATIFY(_pad, __LINE__)[x] \
        _Pragma("clang diagnostic pop")

#define FALSE_SHARING_PADDING PADDING(FALSE_SHARING_PADDING_SIZE)

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

namespace riverq {
	typedef uint64_t u64;
	typedef int64_t i64;
	const std::memory_order relaxed = std::memory_order_relaxed;
	const std::memory_order release = std::memory_order_release;
	const std::memory_order acquire = std::memory_order_acquire;
	const u64 PUB_MASK = (1<<15) - 1;

	inline std::atomic<u64>* alloc_queue(u64 size) {
		std::atomic<u64>* p;
		::posix_memalign((void**)&p, PUB_MASK+1, size*sizeof(u64));
		return p;
	}

	struct spsct;
	extern "C" std::atomic<u64>* publish(std::atomic<u64>* producer, spsct* q);
	extern "C" std::atomic<u64>* advance(std::atomic<u64>* consumer, spsct* q);

	struct spsct {
		std::atomic<u64>* const queue;
		std::atomic<u64>* const end;
		std::atomic<std::atomic<u64>*> published;
		std::atomic<std::atomic<u64>*> consumed;
		FALSE_SHARING_PADDING;
		u64 consumerStall;
		u64 producerStall;
		u64 consumerWrapped;
		u64 producerWrapped;

		inline spsct(u64 size) : queue(alloc_queue(size)), end(queue+size), consumerStall(0), producerStall(0) {
			published.store(queue, release);
			consumed.store(end-(PUB_MASK+1)/sizeof(u64), release);
			// Touch all the pages
			for (u64 i=0; i < size; i += 4096/sizeof(u64)) {
				queue[i].store(0, relaxed);
			}
		}
		inline ~spsct() {
			::free((void*)queue);
		}

		inline void push(std::atomic<u64>*& producer, u64 data) {
			producer++->store(data, relaxed);
			if (unlikely((u64(producer) & PUB_MASK) == 0)) {
				producer = publish(producer, this);
			}
		}

		inline u64 pop(std::atomic<u64>*& consumer) {
			if (unlikely((u64(consumer) & PUB_MASK) == 0)) {
				consumer = advance(consumer, this);
			}
			return consumer++->load(relaxed);
		}
	};

	struct spscl;
	extern "C" std::atomic<u64>* wait_for_consumer(spscl* q, std::atomic<u64>* producer);
	extern "C" std::atomic<u64>* wait_for_producer(spscl* q, std::atomic<u64>* consumer);

	struct spscl {
		std::atomic<u64>* const queue;
		std::atomic<u64>* const end;
		FALSE_SHARING_PADDING;
		u64 consumerStall;
		u64 producerStall;
		u64 consumerWrapped;
		u64 producerWrapped;

		inline spscl(u64 size) : queue(alloc_queue(size+(4096/sizeof(u64))*2)+4096/sizeof(u64)), end(queue+size), consumerStall(0), producerStall(0) {
			::memset(queue, 0, size*sizeof(u64));
			queue[size].store(1, relaxed);
		}

		inline ~spscl() {
			::free((void*)(queue-4096/sizeof(u64)));
		}

		inline void push(std::atomic<u64>*& producer, u64 data) {
			if (unlikely(producer->load(relaxed) != 0))
				producer = wait_for_consumer(this, producer);

			producer++->store(data, relaxed);
		}

		inline u64 pop(std::atomic<u64>*& consumer) {
			u64 val = consumer->load(relaxed);
			if (unlikely(val < 2)) {
				consumer = wait_for_producer(this, consumer);
				val = consumer->load(relaxed);
			}
			consumer[0].store(0, relaxed);
			++consumer;
			return val;
		}
	};
}
