#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include "sfmm.h"
/**
 * All functions you make for the assignment must be implemented in this file.
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */
const int page_size=4096;
const int max_request=16368; //(4*page_size)-16;
int page_count=0;
int internal_frag=0;
int alloc_count=0;
int free_count=0;
int coal_count=0;
void* min_address=0;
void* max_address=0;
sf_free_header* freelist_head = NULL;

void sf_free(void *ptr);//function declarations
void set_header(sf_free_header* freelist_block,unsigned int block_size,unsigned int padding,unsigned int alloc);
void set_footer_find(void* freelist_block,unsigned int block_size,unsigned int alloc);
void* malloc_request(sf_free_header* alloc_ptr,int block_size,int padding);
void coalesce(void* coal_block);
void remove_node(sf_free_header* node);
void insert_node(sf_free_header* insert_node);
int sf_info(info* meminfo);
void* create_space(int block_needed);
sf_free_header* first_fit_search(int block_needed);
sf_free_header* best_fit_search(int block_needed);

void* sf_malloc(size_t size)
{
	if(size<=0)return NULL;
	if(size>max_request)
	{
		errno=ENOMEM;
		return NULL;
	}
	int padding=(size)%16==0?0:16-size%16;
	int block_needed=size+padding+16;

	if(freelist_head==NULL)
	{
		if(page_count==0)
		{
			freelist_head=sf_sbrk(0);//start of space in memory
			min_address=freelist_head;
			max_address=min_address+page_size;
		}
		if(sf_sbrk(1)==((void*)-1))//request space
		{
			errno=ENOMEM;
			return NULL;
		}
		page_count++;
		if(page_count>1)
		{
			freelist_head=max_address;
			max_address+=page_size;
		}
		set_header(freelist_head,page_size,0,0);
		set_footer_find(freelist_head,page_size,0);
	}
	sf_free_header* malloc_ptr=first_fit_search(block_needed);//calls create_space if fails
	if(malloc_ptr==NULL)return NULL;
	else return malloc_request(malloc_ptr,block_needed,padding);
}

void sf_free(void *ptr)
{
	ptr=ptr-8; //get back to header from payload
	if(((sf_free_header*)ptr)->header.alloc==0 || ptr<min_address || ptr>max_address)
	{
		//set ernos
		return;
	}
	else
	{
		int block_size=((sf_free_header*)ptr)->header.block_size<<4;
		set_header(((sf_free_header*)ptr),block_size,0,0);
		set_footer_find(ptr,block_size,0);
		free_count++;
		coalesce(ptr);
	}
}

void* sf_realloc(void *payload, size_t size)
{
	void* ptr=payload-8;
	if(size<=0 || ((sf_free_header*)ptr)->header.alloc==0 || size>max_request || ptr <min_address || ptr>max_address)
	{
		errno=EINVAL;
		return NULL;
	}
	int head_size=((sf_free_header*)ptr)->header.block_size<<4;
	int padding=(size)%16==0?0:16-size%16;
	int total_size=size+padding+16;
	if(total_size == head_size)
	{
		set_header(ptr,total_size,padding,1);
		internal_frag-=((sf_free_header*)ptr)->header.padding_size;
		internal_frag+=padding;
		return payload;
	}
	if(total_size < head_size)
	{
		internal_frag-=((sf_free_header*)ptr)->header.padding_size;
		internal_frag+=padding;
		return malloc_request(ptr,total_size,padding);
	}
	else
	{
		sf_free_header* malloc_ptr=first_fit_search(total_size);//calls create_space if fails
		if(malloc_ptr==NULL)return NULL;
		else 
		{
			malloc_ptr=malloc_request(malloc_ptr,total_size,padding);
			int copy_amt=(((sf_free_header*)ptr)->header.block_size<<4) - (((sf_free_header*)ptr)->header.padding_size+16); 
			memcpy(malloc_ptr,payload,copy_amt);
			sf_free(payload);
			internal_frag-=((sf_free_header*)ptr)->header.padding_size;
			internal_frag+=padding;
			return malloc_ptr;
		}
	}
}

int sf_info(info* meminfo)
{
	int external_frag=0;
	sf_free_header* node=freelist_head;
	while(node!=NULL)
	{
		external_frag+=node->header.block_size<<4;
		node=node->next;
	}
	meminfo->external=external_frag;
	meminfo->internal=internal_frag;
	meminfo->allocations=alloc_count;
	meminfo->frees=free_count;
	meminfo->coalesce=coal_count;
 	return 0;
}


void set_header(sf_free_header* freelist_block,unsigned int block_size,unsigned int padding,unsigned int alloc)
{	
	(freelist_block->header).alloc=alloc;
	(freelist_block->header).padding_size=padding;
	(freelist_block->header).block_size=block_size>>4;
}

void set_footer_find(void* freelist_block,unsigned int block_size,unsigned int alloc)
{
	//casting to footer because free_head doesn't have 
	freelist_block=freelist_block+((((sf_free_header*)(freelist_block))->header).block_size<<4)-8;
	((sf_footer*)(freelist_block))->alloc=alloc;
	((sf_footer*)(freelist_block))->block_size=block_size>>4;
}

void* malloc_request(sf_free_header* alloc_ptr,int block_size,int padding)
{
	int avail_space=(alloc_ptr->header).block_size<<4;
	if(avail_space-block_size<32)block_size=avail_space;

	set_header(alloc_ptr,block_size,padding,1);
	set_footer_find(((void*)alloc_ptr),block_size,1);
	if(block_size<avail_space)
	{
		sf_free_header* new_head=((void*)(alloc_ptr))+(((alloc_ptr->header).block_size)<<4); //alloc_ptr+block_size is the new head
		set_header(new_head,avail_space-block_size,0,0);
		set_footer_find(((void*)new_head),avail_space-block_size,0);
		insert_node(new_head);
	}
	remove_node(alloc_ptr);
	alloc_ptr=(((void*)(alloc_ptr))+8);//get to payload
	internal_frag+=(padding+16);
	alloc_count++;
	return ((void*)(alloc_ptr));
}
void coalesce(void* coal_block) //fix links for coalesce
{
	int init_size=(((sf_free_header*)coal_block)->header.block_size<<4);
	int total_size=init_size;
	//sf_free_header* coal_copy=coal_block;
	void* prev_footer=coal_block-8;//footer of previous block
	void* prev_header=coal_block-(((sf_footer*)prev_footer)->block_size<<4);
	void* next_header=coal_block+(((sf_free_header*)coal_block)->header.block_size<<4);
	void* next_footer=next_header+(((sf_free_header*)next_header)->header.block_size<<4)-8;

	if (prev_footer >= min_address && ((sf_free_header*)prev_header)->header.alloc==0)//prev is free
	{
		//move header 
		total_size+=((sf_free_header*)prev_header)->header.block_size<<4;
		coal_block=prev_header;
		remove_node(((sf_free_header*)prev_header));
	}
	if(next_header<max_address && ((sf_free_header*)next_header)->header.alloc==0) //next is free
	{
		total_size+=((sf_footer*)next_footer)->block_size<<4;
		remove_node(((sf_free_header*)next_header));
	}
	//fix block sizes
	set_header(((sf_free_header*)coal_block),total_size,0,0);
	set_footer_find(coal_block,total_size,0);//try footer_find?

	//set coal_block as front of list
	if(total_size>init_size)//coalescing was done
	{
		//remove_node(coal_copy);
		coal_count++;
	}
	insert_node(((sf_free_header*)coal_block));
}

void remove_node(sf_free_header* node)
{
	if(node==freelist_head)
	{	
		if(node->next==NULL)freelist_head=NULL;
		else 
		{
			freelist_head=node->next;
			freelist_head->prev=NULL;
		}
	}
	else(node->prev)->next=node->next;	
}

void insert_node(sf_free_header* insert_node)
{
	insert_node->next=freelist_head;
	if(freelist_head!=NULL)freelist_head->prev=insert_node;
	freelist_head=insert_node;
}

void* create_space(int block_needed)
{
	while(((freelist_head->header).block_size<<4)<block_needed)//get more space
	{
		if(sf_sbrk(1)==((void*)-1))//no space left
		{
			errno=ENOMEM;
			return NULL;
		}
		page_count++;
		sf_free_header* new_head=max_address;
		max_address+=page_size;//this is here to not increment max twice if list started as NULL
		set_header(new_head,page_size,0,0);
		set_footer_find(((void*)new_head),page_size,0);
		coalesce(((void*)new_head));
	}
	return ((void*)freelist_head);
}

sf_free_header* first_fit_search(int block_needed)
{
	sf_free_header* node=freelist_head;
	while(node!=NULL)
	{
		if( (node->header).block_size<<4 >= block_needed)return node;
		node=node->next;
	}
	return create_space(block_needed);
}

sf_free_header* best_fit_search(int block_needed)
{
	sf_free_header* node=freelist_head;
	sf_free_header* best_fit=NULL;
	int best_diff=INT_MAX;
	while(node!=NULL)
	{
		if( (node->header).block_size<<4 == block_needed)return node;
		if( (node->header).block_size<<4 > block_needed)
		{
			if( ((node->header).block_size<<4) - block_needed < best_diff)
			{
				best_fit=node;
				best_diff=((node->header).block_size<<4) - block_needed;
			}
		}
		node=node->next;
	}
	if(best_fit!=NULL)return best_fit;
	return create_space(block_needed);

}
