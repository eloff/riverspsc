
#include "riverq.hh"

namespace riverq {
	const u64 DEADLOCK_LIMIT = 100000000;
	void deadlock(spsct* q, const char* src, std::atomic<u64>* pos) {
		printf("deadlock detected in %s\n", src);
		printf("begin=%p end=%p published=%p consumed=%p %s=%p\n", q->queue, q->end, q->published.load(), q->consumed.load(), src, pos);
		printf("consumer wrapped=%ld, producer wrapped=%ld\n", q->consumerWrapped, q->producerWrapped);
		exit(-1);
	}

	extern "C" std::atomic<u64>* publish(std::atomic<u64>* producer, spsct* q) {
		if (producer == q->end) {
			q->producerWrapped++;
			producer = q->queue;
		}
		std::atomic<u64>* consumed;
		while (unlikely(producer == (consumed = q->consumed.load(acquire)))) {
			//printf("waiting for consumer to move past %lx\n", u64(consumed));
			if (q->producerStall++ == DEADLOCK_LIMIT)
				deadlock(q, "producer", producer);
			pthread_yield(); //_mm_pause(); // wait for consumer to catch up
		}
		q->published.store(producer, release);
		return producer;
	}

	extern "C" std::atomic<u64>* advance(std::atomic<u64>* consumer, spsct* q) {
		if (consumer == q->end) {
			q->consumerWrapped++;
			consumer = q->queue;
		}
		std::atomic<u64>* published;
		while (unlikely((published = q->published.load(acquire)) == consumer)) {
			//printf("waiting for producer to move past %lx\n", u64(published));
			if (q->consumerStall++ == DEADLOCK_LIMIT)
				deadlock(q, "consumer", consumer);
			pthread_yield(); //_mm_pause(); // let producer get ahead
		}
		q->consumed.store(consumer, release);
		return consumer;
	}

	extern "C" std::atomic<u64>* wait_for_consumer(spscl* q, std::atomic<u64>* producer) {
		if (producer == q->end) {
			q->producerWrapped++;
			producer = q->queue;
		}

		while (producer->load(relaxed) != 0) {
			q->producerStall++;
			pthread_yield();
		}
		return producer;
	}

	extern "C" std::atomic<u64>* wait_for_producer(spscl* q, std::atomic<u64>* consumer) {
		if (consumer == q->end) {
			q->consumerWrapped++;
			//memset(consumer-16, 0, 128);
			consumer = q->queue;
		}

		while (consumer->load(relaxed) == 0) {
			q->consumerStall++;
			pthread_yield();
		}
		return consumer;
	}
}
