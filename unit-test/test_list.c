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

  /* TODO Is this working as designed? */
  l1 = mutt_new_list();
  l1 = mutt_add_list(l1, "Hello");
  TEST_CHECK(l1->data == NULL);
  TEST_CHECK(strcmp(l1->next->data, "Hello") == 0);
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

void test_list_copy()
{
    LIST *l1 = mutt_new_list();
    l1 = mutt_add_list(l1, "Hello");
    l1 = mutt_add_list(l1, "World");
    mutt_push_list(&l1, "Foo");
    mutt_push_list(&l1, "Hello");
    l1 = mutt_add_list(l1, "Neo");
    l1 = mutt_add_list(l1, "Mutt");
    LIST *l2 = mutt_copy_list(l1);
    LIST *tmp1 = l1;
    LIST *tmp2 = l2;
    for (; tmp1 && tmp2; tmp1 = tmp1->next, tmp2 = tmp2->next)
    {
        TEST_CHECK((!(!tmp1->data ^ !tmp2->data)));
        if (tmp1->data)
            TEST_CHECK(strcmp(tmp1->data, tmp2->data) == 0);
    }
    TEST_CHECK(tmp1 == NULL && tmp2 == NULL);
    mutt_free_list(&l1);
    mutt_free_list(&l2);
}
