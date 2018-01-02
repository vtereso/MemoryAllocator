#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "sfmm.h"
/**
 * All functions you make for the assignment must be implemented in this file.
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */
int pageCount=0;
int internal_frag=0;
int alloc_count=0;
int free_count=0;
int coal_count=0;
void* min_address=0;
void* max_address=0;
sf_free_header* freelist_head = NULL;
void sf_free(void *ptr);
void set_header(sf_free_header* freelist_block,unsigned int block_size,unsigned int padding,unsigned int alloc);
void set_footer(void* freelist_block,unsigned int block_size,unsigned int alloc);
void set_footer_find(void* freelist_block,unsigned int block_size,unsigned int alloc);
void oobh(sf_free_header* freelist_header);
void* malloc_request(sf_free_header* alloc_ptr,int block_size,int padding,int alloc);
void coalesce(void* freelist_header, int flag);
void remove_node(sf_free_header* node);
void* find_or_set(int padding,int block_size);

void *sf_malloc(size_t size)
{
	if(size<=0)return NULL;
	if(size>4*4096-16)
	{
		errno=ENOMEM;
		return NULL;
	}
	int padding=(size)%16==0?0:16-size%16;
	int block_needed=size+padding+16;

	if(freelist_head==NULL)
	{
		if(pageCount==0)
		{
			freelist_head=sf_sbrk(0);//I want start of page
			min_address=freelist_head;
			max_address=min_address+4096;
		}
		if(sf_sbrk(1)==((void*)-1))
		{
			errno=ENOMEM;
			return NULL;
		}
		pageCount++;
		if(pageCount>1)
		{
			freelist_head=max_address;
			max_address+=4096;
		}
		//create header/footer for new free block
		set_header(freelist_head,4096,0,0);
		set_footer_find(freelist_head,4096,0);
	}
	sf_free_header* malloc_ptr=find_or_set(padding,block_needed);
	if(malloc_ptr==NULL)return malloc_ptr;
	return malloc_request(malloc_ptr,block_needed,padding,1);
}

void sf_free(void *ptr)
{
	ptr=ptr-8; //get back to header from payload
	if(((sf_free_header*)ptr)->header.alloc==0 || ptr<min_address || ptr>max_address)
	{
		//errno=EINVAL; don't?
		return;
	}
	else
	{
		int block_size=((sf_free_header*)ptr)->header.block_size<<4;
		set_header(((sf_free_header*)ptr),block_size,0,0);
		set_footer_find(ptr,block_size,0);
		free_count++;
		coalesce(ptr,0);
	}
}

void *sf_realloc(void *ptr, size_t size)
{
	void* header_ptr=ptr-8;
	if(size<=0 || ((sf_free_header*)header_ptr)->header.alloc==0 || size>(4*4096)-16 || header_ptr <min_address || header_ptr>max_address)
	{
		errno=EINVAL;
		return NULL;
	}
	int padding=(size)%16==0?0:16-size%16;
	int total_size=size+padding+16;
	void* footer_ptr=header_ptr+total_size-8;
	int head_size=((sf_free_header*)header_ptr)->header.block_size<<4;
	int head_padding=((sf_free_header*)header_ptr)->header.padding_size;
	if(total_size <= head_size)
	{
		if(head_size-total_size<32)
		{
			set_header(((sf_free_header*)header_ptr),total_size,padding,1);
			return ptr;
		}
		else
		{
			//set new alloc
			set_header(((sf_free_header*)header_ptr),total_size,padding,1);
			set_footer(footer_ptr,total_size,1);
			internal_frag-=head_padding;
			internal_frag+=padding;
			alloc_count++; 
			//new free
			set_header(((sf_free_header*)(footer_ptr+8)),head_size-total_size,0,0);
			set_footer_find((footer_ptr+8),head_size-total_size,0);
			coalesce((footer_ptr+8),0);
			return header_ptr+8;
		}
	}
	else
	{
		void* next_block=header_ptr+head_size;
		int next_size=((sf_free_header*)next_block)->header.block_size<<4;
		int link_size=head_size+next_size;

		//able to move footer case DO THIS
		if( ((sf_free_header*)next_block)->header.alloc==0 && link_size>=total_size)
		{
			set_header(((sf_free_header*)header_ptr),total_size,padding,1);
			set_footer_find(header_ptr,total_size,1);
			//set new free
			void* next_ptr=header_ptr+total_size;
			remove_node(next_block);//writing over free NEED TO REMOVE
			if(link_size-total_size>32)
			{
				set_header(((sf_free_header*)next_ptr),(link_size-total_size),0,0);
				set_footer_find(next_ptr,(link_size-total_size),0);
				//put into list
				((sf_free_header*)next_ptr)->next=freelist_head;
				if(freelist_head!=NULL)freelist_head->prev=next_ptr;
				freelist_head=next_ptr;
			}
			return header_ptr+8;
		}

		else
		{
			//test this
			//free block
			sf_free_header* malloc_ptr=find_or_set(padding,total_size);
			if(malloc_ptr==NULL)return malloc_ptr;
			malloc_ptr=malloc_request(malloc_ptr,total_size,padding,1);
			int copy_amt=(((sf_free_header*)header_ptr)->header.block_size<<4)-(((sf_free_header*)header_ptr)->header.padding_size)-16; //space is block-padd-footer
			memcpy(malloc_ptr,ptr,copy_amt);
			internal_frag-=head_padding;
			internal_frag+=padding;
			sf_free(ptr);
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
		external_frag+=node->header.block_size;
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

void set_footer(void* freelist_block,unsigned int block_size,unsigned int alloc)
{
	//casting to footer because free_head doesn't have 
	((sf_footer*)(freelist_block))->alloc=alloc;
	((sf_footer*)(freelist_block))->block_size=block_size>>4;
}
void set_footer_find(void* freelist_block,unsigned int block_size,unsigned int alloc)
{
	//casting to footer because free_head doesn't have 
	freelist_block=freelist_block+((((sf_free_header*)(freelist_block))->header).block_size<<4)-8;
	((sf_footer*)(freelist_block))->alloc=alloc;
	((sf_footer*)(freelist_block))->block_size=block_size>>4;
}

void oobh(sf_free_header* freelist_header)//out of bounds head
{
	if( ((void*)freelist_header)<min_address || ((void*)freelist_header)>max_address)freelist_header=NULL;
}  

void* malloc_request(sf_free_header* alloc_ptr,int block_size,int padding,int alloc)
{
	int flag=0;
	int avail_space=(alloc_ptr->header).block_size<<4;
	if(avail_space-block_size<32)
	{
		block_size=avail_space;
		flag=1;
	}
	set_header(alloc_ptr,block_size,padding,alloc);
	set_footer_find(((void*)alloc_ptr),block_size,alloc);
	if(flag==0)
	{
		//set new free header
		sf_free_header* new_head=((void*)(alloc_ptr))+(((alloc_ptr->header).block_size)<<4); //alloc_ptr+block_size is the new head
		set_header(new_head,avail_space-block_size,0,0);
		set_footer_find(((void*)new_head),avail_space-block_size,0);
		if(new_head!=freelist_head)new_head->next=freelist_head;
		if(freelist_head!=NULL)freelist_head->prev=new_head;
		freelist_head=new_head;
	}
	remove_node(alloc_ptr);
	alloc_ptr=(((void*)(alloc_ptr))+8);
	internal_frag+=(padding+16);
	alloc_count++;
	return ((void*)(alloc_ptr));
}
void coalesce(void* coal_block, int flag) //fix links for coalesce
{
	int init_size=(((sf_free_header*)coal_block)->header.block_size<<4);
	int total_size=init_size;
	void* coal_copy=coal_block;
	void* prev_footer=coal_block-8;//footer of previous block
	void* prev_header=coal_block-(((sf_footer*)prev_footer)->block_size<<4);
	void* next_header=coal_block+(((sf_free_header*)coal_block)->header.block_size<<4);
	void* next_footer=next_header+(((sf_free_header*)next_header)->header.block_size<<4)-8;

	if (prev_footer >= min_address && ((sf_free_header*)prev_header)->header.alloc==0)//prev is free
	{
		//move header 
		total_size+=((sf_free_header*)prev_header)->header.block_size<<4;
		coal_block=prev_header;
		remove_node(prev_header);
	}
	if(next_header<max_address && ((sf_free_header*)next_header)->header.alloc==0) //next is free
	{
		total_size+=((sf_footer*)next_footer)->block_size<<4;
		remove_node(next_header);
	}
	//fix block sizes
	set_header(((sf_free_header*)coal_block),total_size,0,0);
	set_footer((coal_block+total_size-8),total_size,0);//try footer_find?

	//set coal_block as front of list
	if(total_size>init_size)//coalescing was done
	{
		if(flag==1)remove_node(coal_copy);
		coal_count++;
		if(coal_block!=freelist_head)((sf_free_header*)coal_block)->next=freelist_head;
		if(freelist_head!=NULL)freelist_head->prev=coal_block;
	}
	((sf_free_header*)coal_block)->next=freelist_head;//redundant? 
	if(freelist_head!=NULL)freelist_head->prev=coal_block;//r
	freelist_head=coal_block;
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

void* find_or_set(int padding,int block_needed)
{
	sf_free_header* next_node=freelist_head;
	while( next_node!=NULL && (next_node->header).block_size<<4 < block_needed)
	{
		if((next_node->header).block_size<<4 >= block_needed)return ((void*)next_node);
		next_node=next_node->next;
	}

		while(((freelist_head->header).block_size<<4)<block_needed)//get more space
		{
			if(sf_sbrk(1)==((void*)-1))
			{
				errno=ENOMEM;
				return NULL;
			}
			pageCount++;
			sf_free_header* new_head=max_address;
			max_address+=4096;//this is here to not increment max twice if list started as NULL
			set_header(new_head,4096,0,0);
			set_footer_find(((void*)new_head),4096,0);
			coalesce(((void*)new_head),0);
		}
		return ((void*)freelist_head);
}