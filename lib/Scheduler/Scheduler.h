#pragma once
#include <Arduino.h>

// Minimal cooperative task scheduler for the main loop().
// Register periodic tasks once in setup(), then call run() on every
// loop() iteration — it fires each task's function once its interval
// has elapsed, based on millis(). Nothing here blocks or delays, so
// it plays nicely with other subsystems (sensors, display, cam, wifi)
// sharing the same loop().
class Scheduler {
public:
    typedef void (*TaskFn)();

    // Register a task to run every intervalMs. Call from setup().
    void addTask(TaskFn fn, unsigned long intervalMs) {
        if (_taskCount >= MAX_TASKS) return; // silently ignored if full
        _tasks[_taskCount] = { fn, intervalMs, 0 };
        _taskCount++;
    }

    // Call every loop() iteration.
    void run() {
        unsigned long now = millis();
        for (int i = 0; i < _taskCount; i++) {
            Task &t = _tasks[i];
            if ((unsigned long)(now - t.lastRunMs) >= t.intervalMs) {
                t.lastRunMs = now;
                t.fn();
            }
        }
    }

private:
    static const int MAX_TASKS = 8; // bump this if you register more tasks later

    struct Task {
        TaskFn fn;
        unsigned long intervalMs;
        unsigned long lastRunMs;
    };

    Task _tasks[MAX_TASKS];
    int _taskCount = 0;
};