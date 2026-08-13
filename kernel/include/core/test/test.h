#ifndef PROS_TEST_H
#define PROS_TEST_H

#include "stdc.h"

void test_report(const char *tag, const char *name, bool ok);

void test_pmm(void);
void test_heap(void);
void test_vmm(void);
void test_vfs(void);
void test_kstack(void);

#endif //PROS_TEST_H
