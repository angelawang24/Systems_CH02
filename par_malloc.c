
#include <sys/mman.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>

#include <stdlib.h>
#include <unistd.h>

#include "hmalloc.h"
#include "xmalloc.h"

typedef struct bin_t {
	size_t size;
	struct node_t * first;
} bin;

typedef struct node_t {
	size_t size;
	struct node_t * next;
} node;

const size_t PAGE_SIZE = 4096;

__thread bin* arena;

static
size_t
div_up(size_t xx, size_t yy)
{
	size_t zz = xx / yy;
	if (zz * yy == xx) {
		return zz;
	}
	else {
		return zz + 1;
	}
}

void
init_arena()
{
	arena = (void*) mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, 0);			
	bin* start_arena = arena;
	
	int bin_index = 0;
	size_t index_size = 32;
	while (index_size <= PAGE_SIZE) {
		start_arena = (bin*) start_arena;
		start_arena->size = index_size;
		start_arena->first = NULL;

		bin_index++;
		index_size *= 2;

		start_arena++;
	}
	
}

node*
get_pointer_in_node(size_t bytes, node* current)
{
	node* head = current;
	node* prev;
	node* big_enough_block = NULL;

	if (head != 0) {
		size_t size_current = current->size;
		if (bytes <= size_current) { 
			big_enough_block = head;
		} 
		return (node*) big_enough_block;
	}
	return 0;

}

void*
split_rest(size_t bytes, node* ptr, bin* current)
{
	bin* current_bin = current;
	size_t current_size = (size_t)current->size;

	while(bytes != current_size) {
		current_bin--;
		current_size = current_size / 2;
		// second half put into bin
		node* half = (void*) ptr + current_size;
                *((size_t *)half) = current_size;

		half->next = current_bin->first;
		current_bin->first = half;
		
		node* size_ptr = (node*) ptr;
		size_ptr->size = current_size;	
	}
	return (void*)ptr;
}

void*
opt_malloc(size_t bytes)
{
	node* big_enough_block = NULL;

        size_t block_size = 0;
        void* return_ptr;

	if (bytes > PAGE_SIZE) {
		size_t num_pgs = div_up(bytes, PAGE_SIZE);
                big_enough_block = mmap(NULL, PAGE_SIZE * num_pgs, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, 0);
                big_enough_block->size = (PAGE_SIZE * num_pgs);

                return_ptr = (void*) big_enough_block;
                *((size_t *)return_ptr) = big_enough_block->size;
	} else {
		bin* current = (bin*)arena;
		bin* prev = NULL;

		int found_bin = 0;
		size_t index = 0;

		node* success;
		// look for something in the arena that you can use
                while(current->size < PAGE_SIZE && found_bin == 0) {
                        success = get_pointer_in_node(bytes, (node*)current->first);	
			// found something so break out of while loop
			if (success != 0) {
               			current->first = (node*)current->first->next;
				found_bin = 1;                 
                        } else {
                                current++; 
                                index += 1;
                        }
                }
		// found something in a bin
		if (found_bin == 1) {
			if (current->size == bytes) {		
				big_enough_block = (node*)success;
			} else {
				big_enough_block = (node*) split_rest(bytes, success, current);
			}
		} else {
			// didn't find something so need to mmap
			node* new_page = mmap(NULL, PAGE_SIZE * 2, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, 0);
               		current->first = (void*)new_page + PAGE_SIZE;
			current->first->size = PAGE_SIZE;
			new_page->size = PAGE_SIZE;
			// need to put leftovers into correct bins
			big_enough_block = (node*) split_rest(bytes, (node*)new_page, current);
		}
		return_ptr = (node*) big_enough_block; 
	}
	return (void*) return_ptr + sizeof(size_t);
}

size_t 
round_to_next_power_of_two(size_t bytes) 
{
	size_t index_size = 32;
	int reached_correct_size = 0;
        while (index_size <= PAGE_SIZE && reached_correct_size == 0) {
		if (bytes <= index_size) {
			reached_correct_size = 1;
		} else {
			index_size *= 2;
		}
	}
	return index_size;

}

void*
xmalloc(size_t bytes)
{	
	bytes += sizeof(size_t);
	
	if (bytes <= PAGE_SIZE) { 
		bytes = round_to_next_power_of_two(bytes);
	}

	if (arena == NULL) {
		init_arena();
	}
	return opt_malloc(bytes);
}

void
insert_into_bin(node* cell)
{
	size_t size_of_cell = cell->size;
	bin* current_bin = arena;
	
	while(size_of_cell != current_bin->size) {
		current_bin++;
	}
	node* first_current_bin = (node*)current_bin->first;
	
	cell->next = first_current_bin;
	first_current_bin = cell;
	
}
void
opt_free(void* ptr)
{
	node* head_cell = (node*) ptr;
	head_cell->size = round_to_next_power_of_two(head_cell->size);
	if (head_cell->size > PAGE_SIZE) {
		size_t num_pages = div_up(head_cell->size, PAGE_SIZE);
		munmap((void*)head_cell, head_cell->size);
	} else {	
		node* cell = (node*) head_cell;
		insert_into_bin(cell);
	}
}

void
xfree(void* ptr)
{
	opt_free(ptr);
}

void*
opt_realloc(void* prev, size_t bytes)
{
	void* newptr;

	void* item = (void*)prev - sizeof(size_t);
	node* item_node = (node*) item;

	size_t node_size = (size_t) item_node->size;
	newptr = xmalloc(bytes + node_size);
	void* otherptr = memcpy(newptr, prev, node_size);	
	xfree(prev);
	return newptr;
}

void*
xrealloc(void* prev, size_t bytes)
{
    return opt_realloc(prev, bytes);
}

