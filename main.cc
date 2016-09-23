#include "riverq.hh"

#include <stdio.h>
#include <locale.h>

namespace riverq {
#define QUEUE_SIZE (1<<18)    /* 2MB */
#define DATA ((1ul << 30))

#define offsetof __builtin_offsetof

	struct D {
		spscl queue;
		FALSE_SHARING_PADDING;
		std::atomic<u64> push;
		pthread_t push_thread;
		timespec start;
		FALSE_SHARING_PADDING;
		std::atomic<u64> pop;
		pthread_t pop_thread;
		timespec stop;

		inline D() : queue(QUEUE_SIZE), push(0), pop(0) {}
	} data;

	void* push_f(void *) {
		u64 sum = 0;
		std::atomic<u64>* producer(data.queue.queue);
		clock_gettime(CLOCK_MONOTONIC, &data.start);

		u64 i = DATA+1;
		do {
			data.queue.push(producer, i);
			sum += i;
			--i;
		} while(i != 1);
		data.push.store(sum, release);
		return nullptr;
	}

	void* pop_f(void *) {
		u64 val, sum = 0;
		std::atomic<u64>* consumer(data.queue.queue);

		u64 i = DATA+1;
		do {
			val = data.queue.pop(consumer);
			sum += val;
			--i;
		} while(i != 1);

		clock_gettime(CLOCK_MONOTONIC, &data.stop);
		data.pop.store(sum, release);
		return nullptr;
	}

	static void start_thread_and_pin(pthread_t *thread, void* (*func)(void *), void *arg, int core) {
		cpu_set_t mask;
		CPU_ZERO(&mask);
		CPU_SET(core, &mask);
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &mask);
		pthread_create(thread, &attr, func, arg);
	}

	static void join_thread(pthread_t *thread, const char *msg) {
		pthread_join(*thread, nullptr);
	}

	extern "C" int main(int argc, char **argv) {
		int push_core = 1, pop_core = 3;
		if (argc == 3) {
			push_core = atoi(argv[1]);
			pop_core = atoi(argv[2]);
		}

		start_thread_and_pin(&data.push_thread, &push_f, nullptr, push_core);
		start_thread_and_pin(&data.pop_thread, &pop_f, nullptr, pop_core);

		/* Join threads */
		pthread_join(data.push_thread, nullptr);
		pthread_join(data.pop_thread, nullptr);

		if (data.pop.load(acquire) != data.push.load(acquire)) {
			fprintf(stderr, "push sum 0x%lx doesn't match pop sum 0x%lx\n", data.push.load(), data.pop.load());
		} else {
			printf("push sum 0x%lx matches pop sum 0x%lx\n", data.push.load(), data.pop.load());
		}

		double seconds = double(i64(data.stop.tv_sec) - i64(data.start.tv_sec));
		seconds += double((i64(data.stop.tv_nsec) - i64(data.start.tv_nsec))) / 1e9;

		setlocale(LC_NUMERIC, "");
		printf("Consumer stalled %'ld times, Producer stalled %'ld times\n", data.queue.consumerStall, data.queue.producerStall);
		printf("Consumer wrapped %'ld times, Producer wrapped %'ld times\n", data.queue.consumerWrapped, data.queue.producerWrapped);
		printf("\n\nops/sec=%'ld\n", (u64) (DATA / seconds));
		printf("Queue Bandwidth: %10.4f GBytes/s\n",
			   ((double) DATA * sizeof(long)) / (seconds * 1024 * 1024 * 1024));
		printf("Total data sent: %10.4f GBytes\n",
			   ((double) DATA * sizeof(long)) / (1024 * 1024 * 1024));

		return 0;
	}
}
