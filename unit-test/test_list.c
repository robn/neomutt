#include "mutt.h"
#include "lib.h"

void test_list_create()
{
  LIST *l = mutt_new_list();
  TEST_CHECK(l != NULL);
  mutt_free_list(&l);
  TEST_CHECK(l == NULL);
}

void test_list_add()
{
  LIST *l1 = mutt_new_list();
  LIST *l2 = mutt_add_list(l1, "Hello");
  TEST_CHECK(l1 == l2);
  mutt_free_list(&l1);
}

void test_list_find()
{
  LIST *l1 = mutt_new_list();
  LIST *l2 = mutt_find_list(l1, "Hello");
  TEST_CHECK(l2 == NULL);

  mutt_add_list(l1, "Hello");
  l2 = mutt_find_list(l1, "Hello");
  TEST_CHECK(l1 != l2);
  TEST_CHECK(strcmp(l2->data, "Hello") == 0);
  mutt_free_list(&l1);
}

void test_list_push_pop()
{
    LIST *l = mutt_new_list();
    mutt_push_list(&l, "Hello");
    mutt_push_list(&l, "World");
    TEST_CHECK(strcmp(mutt_front_list(l), "World") == 0);
    mutt_pop_list(&l);
    TEST_CHECK(strcmp(mutt_front_list(l), "Hello") == 0);
    mutt_free_list(&l);
}
