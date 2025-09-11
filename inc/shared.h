// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef SHARED_H
#define SHARED_H

#if defined(__NIX)
// TODO these sections not supported?
// static __so_func *__autostart[] __attribute__((section (".CRT$XIU"))) = {
// (__so_func*)__so_ctor }; static __so_func *__autoexit[]
// __attribute__((section (".CRT$XPU"))) = { (__so_func*)__so_dtor };

#define SHARED_OBJECT_API_ENTRY(ctor, dtor)

#elif defined(_WIN32)
#include <windows.h>

typedef int __so_cb(void);
static __so_cb *__so_get_ctor();
static __so_cb *__so_get_dtor();

typedef void __so_func(void);
static void
__so_ctor()
{
	(void)(__so_get_ctor())();
}

static void
__so_dtor()
{
	(void)(__so_get_dtor())();
}

#pragma data_seg(".CRT$XIU")
static __so_func *__autostart[] = { (__so_func *)__so_ctor };
#pragma data_seg(".CRT$XPU")
static __so_func *__autoexit[] = { (__so_func *)__so_dtor };
#pragma data_seg()

#define SHARED_OBJECT_API_ENTRY(ctor, dtor)                                   \
	static __so_cb *__so_get_ctor() { return (__so_cb *)ctor; }           \
	static __so_cb *__so_get_dtor() { return (__so_cb *)dtor; }

#else // better be gcc

#define SHARED_OBJECT_API_ENTRY(ctor, dtor)                                   \
	__attribute__((constructor)) static void __ctor__##ctor(void)         \
	{                                                                     \
		(void)ctor();                                                 \
	}                                                                     \
                                                                              \
	__attribute__((destructor)) static void __dtor__##dtor(void)          \
	{                                                                     \
		(void)dtor();                                                 \
	}

#endif // ifdef _WIN32

#endif // SHARED_H
