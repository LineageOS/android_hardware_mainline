#include <stdlib.h>
#include <iostream>
#include <string>

#include <android/log.h>

using std::cout;
using std::endl;
using std::string;

static string default_tag;

int32_t __android_log_get_minimum_priority(void) __attribute__((weak)) {
    return 0;
}
int32_t __android_log_set_minimum_priority(int32_t) __attribute__((weak)) {
    return 0;
}
void __android_log_set_aborter(__android_aborter_function) __attribute__((weak)) {}
void __android_log_set_logger(__android_logger_function) __attribute__((weak)) {}

void __android_log_call_aborter(const char*) __attribute__((weak)) {
    abort();
}

int __android_log_is_loggable(int, const char*, int) __attribute__((weak)) {
    return 1;
}

void __android_log_logd_logger(const struct __android_log_message* log_message)
        __attribute__((weak)) {
    string tag = log_message->tag ? string(log_message->tag) : default_tag;
    cout << tag << ": " << string(log_message->message) << endl;
}

void __android_log_set_default_tag(const char* tag) __attribute__((weak)) {
    if (tag) default_tag = string(tag);
}

void __android_log_write_log_message(struct __android_log_message* log_message)
        __attribute__((weak)) {
    __android_log_logd_logger(log_message);
}
