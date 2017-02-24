#if HAVE_CONFIG_H
#include "config.h"
#endif

#define main cutest_main
#include "cutest.h"
#undef main

#include "test_list.c"

TEST_LIST = {{"list_create", test_list_create},
             {"list_add", test_list_add},
             {"list_find", test_list_find},
             {0}};
