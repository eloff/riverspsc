#include "riverq.hh"

namespace riverq {
	extern "C" std::atomic<u64>* publish(spsct* q, std::atomic<u64>* producer) {
		if (producer == q->end) {
			q->producerWrapped++;
			producer = q->queue;
		}
		std::atomic<u64>* consumed;
		while (unlikely(producer == (consumed = q->consumed.load(relaxed)))) {
			//printf("waiting for consumer to move past %lx\n", u64(consumed));
			q->producerStall++;
			_mm_pause(); // wait for consumer to catch up
		}
		q->published.store(producer, release);
		return producer;
	}

	extern "C" std::atomic<u64>* advance(spsct* q, std::atomic<u64>* consumer) {
		if (consumer == q->end) {
			q->consumerWrapped++;
			consumer = q->queue;
		}
		std::atomic<u64>* published;
		while (unlikely((published = q->published.load(acquire)) == consumer)) {
			//printf("waiting for producer to move past %lx\n", u64(published));
			q->consumerStall++;
			_mm_pause(); // let producer get ahead
		}
		q->consumed.store(consumer, relaxed);
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
			memset(consumer-16, 0, 128);
			consumer = q->queue;
		}

		while (consumer->load(relaxed) == 0) {
			q->consumerStall++;
			pthread_yield();
		}
		return consumer;
	}
}