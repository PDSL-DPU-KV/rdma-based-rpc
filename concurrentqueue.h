#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MOODYCAMEL_EXPORT
#ifdef _WIN32
#if defined(MOODYCAMEL_STATIC) // preferred way
#define MOODYCAMEL_EXPORT
#elif defined(DLL_EXPORT)
#define MOODYCAMEL_EXPORT __declspec(dllexport)
#else
#define MOODYCAMEL_EXPORT __declspec(dllimport)
#endif
#else
#define MOODYCAMEL_EXPORT
#endif
#endif

typedef void *MoodycamelCQHandle;
typedef void *MoodycamelValue;
typedef void *MoodycamelToken;

MOODYCAMEL_EXPORT int moodycamel_cq_create(MoodycamelCQHandle *handle);
MOODYCAMEL_EXPORT int moodycamel_cq_destroy(MoodycamelCQHandle handle);

MOODYCAMEL_EXPORT int moodycamel_cq_enqueue(MoodycamelCQHandle handle,
                                            MoodycamelValue value);
MOODYCAMEL_EXPORT int
moodycamel_cq_enqueue_with_token(MoodycamelCQHandle handle,
                                 MoodycamelToken prod_token,
                                 MoodycamelValue value);
MOODYCAMEL_EXPORT size_t moodycamel_cq_enqueue_bulk_with_token(
    MoodycamelCQHandle handle, MoodycamelToken prod_token,
    MoodycamelValue *values, size_t num);

MOODYCAMEL_EXPORT int moodycamel_cq_try_dequeue(MoodycamelCQHandle handle,
                                                MoodycamelValue *value);
MOODYCAMEL_EXPORT int
moodycamel_cq_try_dequeue_with_token(MoodycamelCQHandle handle,
                                     MoodycamelToken cons_token,
                                     MoodycamelValue *value);
MOODYCAMEL_EXPORT size_t moodycamel_cq_try_dequeue_bulk(
    MoodycamelCQHandle handle, MoodycamelValue *values, size_t max);
MOODYCAMEL_EXPORT size_t moodycamel_cq_try_dequeue_bulk_with_token(
    MoodycamelCQHandle handle, MoodycamelToken cons_token,
    MoodycamelValue *values, size_t max);
MOODYCAMEL_EXPORT size_t moodycamel_cq_size_approx(MoodycamelCQHandle handle);

MOODYCAMEL_EXPORT int moodycamel_cons_token(MoodycamelCQHandle handle,
                                            MoodycamelToken *token);
MOODYCAMEL_EXPORT int moodycamel_prod_token(MoodycamelCQHandle handle,
                                            MoodycamelToken *token);
MOODYCAMEL_EXPORT int moodycamel_cons_token_destroy(MoodycamelToken token);
MOODYCAMEL_EXPORT int moodycamel_prod_token_destroy(MoodycamelToken token);

#ifdef __cplusplus
}
#endif
