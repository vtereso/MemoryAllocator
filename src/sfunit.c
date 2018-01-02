#include <criterion/criterion.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "sfmm.h"

/**
 *  HERE ARE OUR TEST CASES NOT ALL SHOULD BE GIVEN STUDENTS
 *  REMINDER MAX ALLOCATIONS MAY NOT EXCEED 4 * 4096 or 16384 or 128KB
 */

Test(sf_memsuite, Malloc_an_Integer, .init = sf_mem_init, .fini = sf_mem_fini) {
    int *x = sf_malloc(sizeof(int));
    *x = 4;
    cr_assert(*x == 4, "Failed to properly sf_malloc space for an integer!");
}

Test(sf_memsuite, Free_block_check_header_footer_values, .init = sf_mem_init, .fini = sf_mem_fini) {
    void *pointer = sf_malloc(sizeof(short));
    sf_free(pointer);
    pointer = pointer - 8;
    sf_header *sfHeader = (sf_header *) pointer;
    cr_assert(sfHeader->alloc == 0, "Alloc bit in header is not 0!\n");
    sf_footer *sfFooter = (sf_footer *) (pointer - 8 + (sfHeader->block_size << 4));
    cr_assert(sfFooter->alloc == 0, "Alloc bit in the footer is not 0!\n");
}

Test(sf_memsuite, PaddingSize_Check_char, .init = sf_mem_init, .fini = sf_mem_fini) {
    void *pointer = sf_malloc(sizeof(char));
    pointer = pointer - 8;
    sf_header *sfHeader = (sf_header *) pointer;
    cr_assert(sfHeader->padding_size == 15, "Header padding size is incorrect for malloc of a single char!\n");
}

Test(sf_memsuite, Check_next_prev_pointers_of_free_block_at_head_of_list, .init = sf_mem_init, .fini = sf_mem_fini) {
    int *x = sf_malloc(4);
    memset(x, 0, 4);
    cr_assert(freelist_head->next == NULL);
    cr_assert(freelist_head->prev == NULL);
}

Test(sf_memsuite, Coalesce_no_coalescing, .init = sf_mem_init, .fini = sf_mem_fini) {
    void *x = sf_malloc(4);
    void *y = sf_malloc(4);
    memset(y, 0xFF, 4);
    sf_free(x);
    cr_assert(freelist_head == x-8);
    sf_free_header *headofx = (sf_free_header*) (x-8);
    sf_footer *footofx = (sf_footer*) (x - 8 + (headofx->header.block_size << 4)) - 8;

    sf_blockprint((sf_free_header*)((void*)x-8));
    // All of the below should be true if there was no coalescing
    cr_assert(headofx->header.alloc == 0);
    cr_assert(headofx->header.block_size << 4 == 32);
    cr_assert(headofx->header.padding_size == 0);

    cr_assert(footofx->alloc == 0);
    cr_assert(footofx->block_size << 4 == 32);
}

/*
//############################################
// STUDENT UNIT TESTS SHOULD BE WRITTEN BELOW
// DO NOT DELETE THESE COMMENTS
//############################################
*/

Test(sf_memsuite, Test_case_1, .init = sf_mem_init, .fini = sf_mem_fini)//realloc check padding 
{
    void *x = sf_malloc(4);
    void *y = sf_malloc(4);//space in the way
    memset(y, 0xFF, 4);
    x=sf_realloc(x,5);//realloc for more space splinter
    sf_free_header *headofx = ((sf_free_header*) (x-8));
    cr_assert(headofx->header.padding_size == 11);
    x=sf_realloc(x,17);//realloc for more space+has to move from original pos
    headofx = ((sf_free_header*) (x-8));
    cr_assert(headofx->header.padding_size == 15);
    x=sf_realloc(x,1);//realloc for less space and coalesce must occur
    headofx = ((sf_free_header*) (x-8));
    cr_assert(headofx->header.padding_size == 15);
}

Test(sf_memsuite, Test_case_2, .init = sf_mem_init, .fini = sf_mem_fini)//realloc maintain
{
    int value=12345678;
    int *x = sf_malloc(100);
    *x=value;//assign value
    x = sf_realloc(x,1000);
    cr_assert(*x==value);//ensure value was copied
}

Test(sf_memsuite, Test_case_3, .init = sf_mem_init, .fini = sf_mem_fini)//free check memory saved
{
    void *a=sf_malloc(50);
    void *b=sf_malloc(50);
    void *c=sf_malloc(50);
    void *d=sf_malloc(50);
    sf_free(b);
    sf_free(d);
    sf_free(c);
    sf_free_header* check_space=((sf_free_header*)(a-8));
    sf_free(a);
    cr_assert((check_space->header.block_size<<4)==4096);//ensure no space was lost
}

Test(sf_memsuite, Test_case_4, .init = sf_mem_init, .fini = sf_mem_fini)//realloc move footer
{
    void *a=sf_malloc(4080);//one page
    void *b=sf_malloc(4096);//2 pages
    sf_free(b);
    a=sf_realloc(a,5000);// move footer of b free block realloc
    sf_free_header* a_ptr=((sf_free_header*)(a-8));
    cr_assert(a_ptr->header.padding_size=8);//ensure block reallocated correctly
    cr_assert(a_ptr->header.block_size=5024);
}

Test(sf_memsuite, Test_case_5, .init = sf_mem_init, .fini = sf_mem_fini)//realloc with page coalesce
{
    void* a=sf_malloc(4);
    a=sf_realloc(a,5008);
    sf_free_header* a_ptr=((sf_free_header*)(a-8));
    cr_assert(a_ptr->header.block_size<<4==5024);//ensure block reallocated correctly this time realloc calls a page then coalesces
    cr_assert((((void*)freelist_head)+32)==a_ptr);//ensure freelist points to old a malloc call and that realloc is in correct space
}

Test(sf_memsuite, Test_case_6, .init = sf_mem_init, .fini = sf_mem_fini)//realloc maintain free list
{
    void* a=sf_malloc(1000);
    void* b=sf_malloc(1000);
    memset(b, 0xFF, 4);//remove errors
    sf_free_header* start=((sf_free_header*)(a-8));
    a=sf_realloc(a,2000);//pulling from freeblock in this case
    cr_assert(freelist_head==start);
    start=((sf_free_header*)(a-8));
    int add=start->header.block_size<<4;
    start=((sf_free_header*)(((void*)start)+add));
    cr_assert(freelist_head->next==start);
}

Test(sf_memsuite, Test_case_7, .init = sf_mem_init, .fini = sf_mem_fini) //free list is correct w spaces
{
    void *a=sf_malloc(50);
    memset(a, 0xFF, 4);//remove errors
    void *b=sf_malloc(50);
    void *c=sf_malloc(50);
    memset(c, 0xFF, 4);//remove errors
    void *d=sf_malloc(50);
    void *e=sf_malloc(50);
    memset(e, 0xFF, 4);//remove errors
    void *f=sf_malloc(50);
    memset(f, 0xFF, 4);//remove errors
    void *g=sf_malloc(50);
    void *h=sf_malloc(50);
    //free blocks/list size=1 
    sf_free(b);//+1
    sf_free(d);//+1
    sf_free(g);//+1
    sf_free(h);//-1
    int free_count=3;
    sf_free_header* node=freelist_head;
    while(node!=NULL)
    {
        free_count--;
        node=node->next;
    }
    cr_assert(free_count==0);
}

Test(sf_memsuite, Test_case_8, .init = sf_mem_init, .fini = sf_mem_fini) // free invalids
{
    void* a=sf_malloc(4);
    sf_free(a);
    sf_free_header* free_head=freelist_head;
    sf_free(a);//double free
    sf_free_header* free_head2=freelist_head;
    cr_assert(free_head==free_head2); 
    void* thing=sf_malloc(1);
    sf_free(thing);
    thing+=4096;//invalid range
    free_head2=freelist_head;
    cr_assert(free_head==free_head2); 
}

Test(sf_memsuite, Test_case_9, .init = sf_mem_init, .fini = sf_mem_fini) //realloc splinter
{
    void* a=sf_malloc(4060);
    sf_free_header* ptr=(sf_free_header*)(a-8);
    cr_assert(ptr->header.block_size<<4==4096); 
}

Test(sf_memsuite, Test_case_10, .init = sf_mem_init, .fini = sf_mem_fini) //malloc all 4 pages
{
    void* a=sf_malloc((4*4096)-16);
    sf_free_header* ptr=(sf_free_header*)(a-8);
    cr_assert(ptr->header.block_size<<4==4096*4);
}

Test(sf_memsuite, Test_case_11, .init = sf_mem_init, .fini = sf_mem_fini) //erno checks
{
    void* thing=sf_malloc(4*4096);
    cr_assert(errno==ENOMEM);
    thing=sf_realloc(thing,0);
    cr_assert(errno==EINVAL);
}

Test(sf_memsuite, Test_case_12, .init = sf_mem_init, .fini = sf_mem_fini) //malloc 4 pages individually check freelist
{
    void* page1=sf_malloc(4080);
    memset(page1, 0xFF, 4);//remove errors
    void* page2=sf_malloc(4080);
    memset(page2, 0xFF, 4);//remove errors
    void* page3=sf_malloc(4080);
    memset(page3, 0xFF, 4);//remove errors
    void* page4=sf_malloc(4080);
    memset(page4, 0xFF, 4);//remove errors
    cr_assert(freelist_head==NULL);
}

Test(sf_memsuite, Test_case_13, .init = sf_mem_init, .fini = sf_mem_fini) // go crazy doing everything
{
    void* page1=sf_malloc(4080);
    memset(page1, 0xFF, 4);//remove errors
    void* page2=sf_malloc(4080);
    memset(page2, 0xFF, 4);//remove errors
    void* page3=sf_malloc(4080);
    memset(page3, 0xFF, 4);//remove errors
    void* page4=sf_malloc(4080);
    memset(page4, 0xFF, 4);//remove errors
    sf_free(page1);
    sf_free(page2);
    sf_free(page3);
    sf_free(page4);
    void* x=sf_malloc(4*4096-16);
    sf_free(x);
    cr_assert((freelist_head->header.block_size<<4)==(4096*4));
}
