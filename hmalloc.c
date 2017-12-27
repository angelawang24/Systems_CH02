#include <stdlib.h>
#include <sys/mman.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include "hmalloc.h"

/*
  typedef struct hm_stats {
  long pages_mapped;
  long pages_unmapped;
  long chunks_allocated;
  long chunks_freed;
  long free_length;
  } hm_stats;
*/

// size field includes sizeof(size_t)
typedef struct node {
	size_t size;
	struct node * next;
} node;

node* head = NULL;

const size_t PAGE_SIZE = 4096;
static hm_stats stats; // This initializes the stats to 0.

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

long
free_list_length()
{
	long length = 0;
	node* current = head;
	node* previous = NULL;
	if (current == NULL) {
		return length;
	} else {
		while(current != NULL){
			length++;
			current = current->next;
		}
		return length;
	}
}

hm_stats*
hgetstats()
{
	stats.free_length = free_list_length();
	return &stats;
}

void
hprintstats()
{
	stats.free_length = free_list_length();
	fprintf(stderr, "\n== husky malloc stats ==\n");
	fprintf(stderr, "Mapped:   %ld\n", stats.pages_mapped);
	fprintf(stderr, "Unmapped: %ld\n", stats.pages_unmapped);
	fprintf(stderr, "Allocs:   %ld\n", stats.chunks_allocated);
	fprintf(stderr, "Frees:    %ld\n", stats.chunks_freed);
	fprintf(stderr, "Freelen:  %ld\n", stats.free_length);
}

static
size_t
div_up(size_t xx, size_t yy)
{
	// This is useful to calculate # of pages
	// for large allocations.
	size_t zz = xx / yy;

	if (zz * yy == xx) {
		return zz;
	}
	else {
		return zz + 1;
	}
}

void
coalesce(node* big_enough_block)
{
	node* end_of_block = big_enough_block->size + big_enough_block + sizeof(size_t);
	node* next = big_enough_block->next;
	if (end_of_block == next) {
		size_t size = big_enough_block->size + next->size + sizeof(node);
		big_enough_block->size = size;
		big_enough_block->next = next->next;
	}
}

void
insert(node* big_enough_block)
{
	node* current = head;
	node* previous = NULL;

	if (head == NULL) {
		head = big_enough_block;
		head->next = NULL;

	} else if ((big_enough_block) < head && (head!= 0)) {
		big_enough_block->next = head;
		head = big_enough_block;
		head->next = NULL;

	} else {
		while(current->next != 0) {	
			previous = current;
			current = current->next;
			if (previous < big_enough_block && current > big_enough_block) {			
				previous->next = big_enough_block;
				big_enough_block->next = current;
				current->next = NULL;
				break;
			}
		}
	}
	coalesce(big_enough_block);
}

void*
hmalloc(size_t size)
{	
	pthread_mutex_lock(&mutex);
	stats.chunks_allocated += 1;
	size += sizeof(size_t);

	node* big_enough_block = NULL;

	size_t block_size = 0;
	void* return_ptr;
	
	if (size < PAGE_SIZE) {
		node* current = head;
		node* previous = NULL;
	
		// go through free list until find something big enough or reach the end
		while((current != 0) && (current->size < size)) {
			previous = current;			
			current = current->next;
		}

		// found match
		if ((current != NULL) && (current->size >= size)) {
			big_enough_block = current;

			if (head == current && current->next != 0) {
				head = current->next;
			} else if (current == head){
				head = 0;
			} else {
				previous->next = current->next;
			}
		} else if (big_enough_block == 0) {
			// allocate more stuff
			big_enough_block = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, 0);	
			big_enough_block->size = PAGE_SIZE;
			big_enough_block->next = 0;
			stats.pages_mapped += 1;
		}

		if (big_enough_block->size - size >= sizeof(node)) {
			big_enough_block->size -= size;
			insert(big_enough_block);
		} else {
			size = big_enough_block->size;
			big_enough_block->size = 0;
		}	
		return_ptr = (void*) big_enough_block + big_enough_block->size;
		*((size_t *)return_ptr) = size;
		
	} else {
		size_t num_pgs = div_up(size, PAGE_SIZE); 
		big_enough_block = mmap(NULL, PAGE_SIZE * num_pgs, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, 0);
		big_enough_block->size = (PAGE_SIZE * num_pgs);
		stats.pages_mapped += num_pgs;
	
		return_ptr = (void*) big_enough_block;
		*((size_t *)return_ptr) = big_enough_block->size;		
	}
	pthread_mutex_unlock(&mutex);
	return (void*) return_ptr + sizeof(size_t);	
}

void
hfree(void* item)
{
	pthread_mutex_lock(&mutex);
	stats.chunks_freed += 1;
	item = item - sizeof(size_t);
	
	node* item_node = (node*) item;
	
	if (item_node->size < PAGE_SIZE) {
		insert(item_node);
	} else {
		size_t num_pgs = div_up(item_node->size, PAGE_SIZE);
		munmap(item, num_pgs);

		stats.pages_unmapped += num_pgs;
	}
	pthread_mutex_unlock(&mutex);	
}

void* 
hrealloc(void* prev, size_t bytes)
{
	pthread_mutex_lock(&mutex);
	void* newptr;

	void* item = prev - sizeof(size_t);
	node* item_node = (node*) item;

	size_t node_size = (size_t) item_node->size;
	
	pthread_mutex_unlock(&mutex);
	newptr = hmalloc(bytes + node_size);
	void* otherptr = memcpy(newptr, prev, node_size);
	hfree(prev);

	return newptr;

}
