#include <obs-module.h>
#include <util/platform.h>
#include <util/threading.h>
#include <util/bmem.h>
#include "plugin-macros.generated.h"
#include "face-detector-base.h"
#ifndef _WIN32
#include <sys/time.h>
#include <sys/resource.h>
#else // _WIN32
#include <windows.h>
#endif // _WIN32

face_detector_base::face_detector_base()
{
	pthread_mutex_init(&mutex, NULL);
	pthread_cond_init(&cond, NULL);
	request_stop = 0;
	running = 0;
	leak_test = bmalloc(1);
}

face_detector_base::~face_detector_base()
{
	stop();
	bfree(leak_test);
	pthread_cond_destroy(&cond);
	pthread_mutex_destroy(&mutex);
}

void *face_detector_base::thread_routine(void *p)
{
	face_detector_base *base = (face_detector_base *)p;
#ifndef _WIN32
	setpriority(PRIO_PROCESS, 0, 19);
#else  // _WIN32
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
#endif // _WIN32
	os_set_thread_name("face-det");

	base->lock();
	while (!base->request_stop.load(std::memory_order_acquire)) {
		try {
			base->detect_main();
		} catch (std::exception &e) {
			blog(LOG_ERROR, "detect_main: exception %s", e.what());
		} catch (...) {
			blog(LOG_ERROR, "detect_main: unknown exception");
		}
		pthread_cond_wait(&base->cond, &base->mutex);
	}
	base->unlock();
	return NULL;
}

void face_detector_base::start()
{
	if (running.load(std::memory_order_acquire))
		return;

	blog(LOG_INFO, "face_detector_base: starting the thread.");
	request_stop.store(false, std::memory_order_release);
	int err = pthread_create(&thread, NULL, thread_routine, (void *)this);
	if (err) {
		blog(LOG_ERROR, "face_detector_base: pthread_create failed (%d)", err);
		return;
	}
	running.store(true, std::memory_order_release);
}

void face_detector_base::stop()
{
	blog(LOG_INFO, "face_detector_base: stopping the thread...");
	lock();
	request_stop.store(true, std::memory_order_release);
	signal();
	unlock();
	if (running.load(std::memory_order_acquire)) {
		pthread_join(thread, NULL);
		running.store(false, std::memory_order_release);
	}
	blog(LOG_INFO, "face_detector_base: stopped the thread...");
}
