/*
 * mm.c
 *
 * Name: Geng Niu 
 *
 * My design is using the segregate free list:
 * It contains 10 explicit free list in the freelists array and for each of the size interval used the 2^n size and adapt it with our minimum block size design.
 * 
 * Heap Design:
 * The heap start with a always allocated prelogue block (header and footer) AND end with always allocated size 0 epilogue block have only a header.
 * The normal block will have:
 * [8bytes header + real payload data block (Min_payload size is 16) + 8 bytes footer]
 * The header contains size in first 7 bytes and last bytes is marker of free / allocated.
 * After the init: 8bytes padding + (header ----8bytes---- footer ----8bytes----) + [every new block] + (epilogue ----8bytes----)
 * 
 * malloc Design:
 * There are serval helper functions that can obtain the size of the block, the pointer to payload, pointer to next block and block checker for allocated or not etc.
 * Malloc Logic Brief:
 * Once the malloc called, it will check the valid and alignment of the size that user asking. 
 * Then it will use the aligned size (including the header and footer size) to find the free block in free list by find_firstfit_in_free_list function.
 * If found, the find_firstfit_in_free_list function will return the ptr that points to the free block's payload.
 *           then remove the select free block from the free list first, then split_and_allocate_block will split, allocate 
 *           and put extra free block back to free list, if the left extra is larger or equal to minimum block size.
 * If not found, the find_firstfit_in_free_list function will return NULL,
 *           then it will use the mm_sbrk in expand_heap to expand new space for the block.
 *           and also use split_and_allocate_block function to allocate the block.
 * 
 * free Design:
 * The free function will firstly mark the curr_block's header and footer to free (0),
 * then use the merge() function to merge the prev or next block that is also free,
 * If there is no other free block nearby,
 *           the merge will add the current free block to free list and go to the end of free function.
 * If there are free blocks nearby,
 *           the merge will remove the nearby free block from the free list first
 *           then it will merge them to one big free block
 *           then it will add the bigger free block back to free list again.
 * 
 * realloc Design:
 * The realloc function will check parameters size and ptr first, 
 * then it will get the minimum number among size and the block pointed by oldptr.
 * then just use malloc() we inplemented, to arrange a new sapce for it, old space will be free.
 * malloc will do the find first fit or expand new space in heap.
 * 
 *
 * Now the utilitization is 58.8% and thoughut is 21864 kops/sec.
 * Checkpoint 1 is 50/50, checkpoint 2 is 100/100 and final score is 61-63/100
 *
 * Malloclab is hard, but I made it.
 * I learned a lot from Prof.Zhu and TAs and achieved perfect scores in the first two checkpoints! 
 * 
 */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

#include "mm.h"
#include "memlib.h"

/*
 * If you want to enable your debugging output and heap checker code,
 * uncomment the following line. Be sure not to have debugging enabled
 * in your final submission.
 */
//#define DEBUG

#ifdef DEBUG
// When debugging is enabled, the underlying functions get called
#define dbg_printf(...) printf(__VA_ARGS__)
#define dbg_assert(...) assert(__VA_ARGS__)
#else
// When debugging is disabled, no code gets generated
#define dbg_printf(...)
#define dbg_assert(...)
#endif // DEBUG

// do not change the following!
#ifdef DRIVER
// create aliases for driver tests
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#define memset mm_memset
#define memcpy mm_memcpy
#endif // DRIVER

#define ALIGNMENT 16

// rounds up to the nearest multiple of ALIGNMENT
static size_t align(size_t x)
{
    return ALIGNMENT * ((x+ALIGNMENT-1)/ALIGNMENT);
}

//here are heap pointer to the beginning of heap, prelogure and epilogue blocks
//All the information are stored in the header and footer of block
uint64_t* heap_pre_before_padding;
uint64_t* heap_pre;
uint64_t* heap_epi;

//Define the header and footer size, also define the number of freelist in the freelist_array
#define header_size 8 //header and footer are always 8 bytes.
#define footer_size 8
#define free_list_num 10


//Here is the explicit free list struct, it provides prev* and next*.
//the prev and next ptr point to the previous and next free block in the heap
typedef struct node_t
{   
    struct node_t* prev;
    struct node_t* next;
}node_t;

//Init the freelist_heads, since the freelist is doubly linked list,
//it can be init by set the head to NULL
node_t* freelist_heads[free_list_num];

void free_list_array_init(){
    for(int i = 0; i < free_list_num; i++){
        freelist_heads[i] = NULL;
    }
    //This function will be call when mm_init to init the freelist array with heap at the same time.
}

int go_which_range_freelist(uint64_t size){
    //NOTE: the size includes header and footer size, it's sync with my implicit free list design.
    //The minimum block size is 32, so lowest range will start at 64 bytes.
    if(size <= 64){
        return 0;
    }
    else if(size <= 128){
        return 1;
    }
    else if(size <= 256){
        return 2;
    }
    else if(size <= 512){
        return 3;
    }
    else if(size <= 1024){
        return 4;
    }
    else if(size <= 2048){
        return 5;
    }
    else if(size <= 4096){
        return 6;
    }
    else if(size <= 8192){
        return 7;
    }
    else if(size <= 16384){
        return 8;
    }
    else{
        return 9;
    }
}

//Here is the add_to_freelist and remove_from_freelist functions.
//They will get the array index by the block size to add/remove in corresponse free list. 
//node_ptr is ptr to payload location.
void add_to_freelist(uint64_t* node_ptr, uint64_t size){
    int freelist_array_index = go_which_range_freelist(size);
    node_t* currentnode = (node_t*) node_ptr;

    if(freelist_heads[freelist_array_index] == NULL){
        freelist_heads[freelist_array_index] = currentnode;
        freelist_heads[freelist_array_index]->prev = NULL;
        freelist_heads[freelist_array_index]->next = NULL;
    }
    else if (freelist_heads[freelist_array_index] != NULL){
        currentnode->prev = NULL;
        currentnode->next = freelist_heads[freelist_array_index];
        freelist_heads[freelist_array_index]->prev = currentnode;
        freelist_heads[freelist_array_index] = currentnode;
    }
}

//node_ptr is ptr to payload location.
//because remove is hard for me, so detail explaination with visualization here.
void remove_from_freelist(uint64_t* node_ptr, uint64_t size){
    int freelist_array_index = go_which_range_freelist(size);
    node_t* currentnode = (node_t*) node_ptr;

    if (currentnode->prev != NULL && currentnode->next != NULL){
        currentnode->prev->next = currentnode->next;
        currentnode->next->prev = currentnode->prev;
        //remove the block in middle of free block list
        //prev_block - curr_block - next_block
        //the prev_block(head)'s next will be the next_block.
        //prev_block - (curr_block) - next_block
        //and next_block prev point to prev_block(head).
        //prev_block - next_block
    }
    else if((currentnode->prev == NULL && currentnode->next == NULL)){
        freelist_heads[freelist_array_index] = NULL;
        //this is remove the only element in the freelist.
        //so just make freelist_head = null to make the list empty.
    }
    else if(currentnode->prev == NULL){
        freelist_heads[freelist_array_index] = currentnode->next;
        freelist_heads[freelist_array_index]->prev = NULL;
        //remove the block in the beginning of freeblock list
        //curr_block(head) - next_block
        //the prev_block's next will be the head.
        //curr_block - next_block(new head)
        //and make new head prev point to NULL.
        //NULL - next_block(new head)
    }
    else if(currentnode->next == NULL){
        currentnode->prev->next = NULL;
        //remove the block in the end of the free block list
        //prev_block - curr_block - NULL
        //we just make prev_block's next point to NULL
        //that makes it new end block of the free block list.
    }
}


/*
 * mm_init: returns false on error, true on success.
 */
bool mm_init(void)
{
    // IMPLEMENT THIS
    // Initialize the freelist array.
    free_list_array_init();

    // we need allocate 4 blocks of size for padding, prelogue and eqilogue.
    heap_pre_before_padding = (uint64_t *)mm_sbrk(32);
    if(heap_pre_before_padding ==(void *) -1){
        return false;
    } 

    heap_pre = (uint64_t*)((char*)heap_pre_before_padding + header_size);
    //We add 8bytes padding at the beginning to align with 16 bytes.
    //8bytes padding + (header ----8bytes---- footer ----8bytes----) + + (epilogue ----8bytes----)

    *heap_pre = (header_size + footer_size) | 0x1;    //header of prelogue
    *(uint64_t *)((char*)heap_pre + header_size) = (header_size + footer_size)  | 0x1;    //footer of prelogue

    heap_epi = (uint64_t*)((char*) heap_pre + header_size + footer_size);    //header pointer of epilogue, and it doesnt have footer.
    *heap_epi = 0x0000000000000000 | 0x0000000000000001;   //Epilogue value: 0x1

    return true;
    //We apply 32 bytes space, but only use 24 bytes. 32 satisfy the alignment of 16 bytes.
}


//helper functions

void put(uint64_t *p, uint64_t value){
    *(uint64_t *) p = value;
    //dereference pointer to change the value at the addr of *p.
}

uint64_t pack(uint64_t size, uint64_t alloc_status){
    return (size | alloc_status);
    //Pack the header/footer data for you.
    //example block size is 10096 and it's free 
    //means 0x0000000000002770 | 0x0000000000000001 = 0x0000000000002771
}

uint64_t get_total_block_size(uint64_t* block_ptr){
    return *block_ptr & 0xFFFFFFFFFFFFFFF0;
    // 0xFFFFFFFFFFFFFFF0 is 1111...60's 1...0000, using & will only remain the 1 in the header and ingore the last bit f/a
}

uint64_t is_block_allocated(uint64_t* block_ptr){
    return *block_ptr & 0x000000000000000F;
    //0x000000000000000F is 0000000...60's 0...1111, using & will check weather last bit is 0 or 1. if 1 return 1 acclocated, if 0 return 0 free.
}


uint64_t* get_next_block(uint64_t* block_ptr){
    return (uint64_t*)((char*)block_ptr + get_total_block_size(block_ptr));
    //convert uint64_t to char to better calculate the next block addr.
}

uint64_t* get_payload_ptr(uint64_t* block_ptr){
    return (uint64_t*)((char*)block_ptr + header_size);
    //Because block_ptr is a uint64_t, each uint64 is 8 bytes, so plus 1 move from beginning of blcok to after header, beginning of data space.
}

uint64_t* get_header_ptr(uint64_t* payload_ptr){
    return (uint64_t*)((char*)payload_ptr - header_size);
}


// Split and allocate block is where the block got allocated and extra will be set back to free and add back to free list.
uint64_t* split_and_allocate_block(uint64_t* block_ptr, uint64_t allocating_size){
    if (block_ptr == NULL){
        return NULL;
    }

    //the block_allocating is the block we found free and larger than asked size allocating_size + 32bytes (1 pair header footer for data,1 pair for rest of free block) 
    uint64_t block_allocating = get_total_block_size(block_ptr);
    if (block_allocating >= ((uint64_t)allocating_size + header_size + footer_size + header_size + footer_size)){         //32 is for minimum size of block
        put(block_ptr,(pack(allocating_size,1)));
        uint64_t* footer_ptr = (uint64_t*)((char*)block_ptr + allocating_size - footer_size);
        put(footer_ptr,pack(allocating_size,1));
        //Allocated block header and footer setting.

        uint64_t left_free_block_size = block_allocating - allocating_size;
        uint64_t* left_free_block_ptr = (uint64_t*)((char*)block_ptr + allocating_size);
        put(left_free_block_ptr,pack(left_free_block_size,0));    
        uint64_t free_payload_left_size = left_free_block_size - footer_size - header_size;
        uint64_t* left_free_block_footer_ptr = (uint64_t*)((char*)left_free_block_ptr + free_payload_left_size + header_size);
        put(left_free_block_footer_ptr,pack(left_free_block_size,0));
        //Left free extra block header and footer setting.
        add_to_freelist(get_payload_ptr(left_free_block_ptr), left_free_block_size);
        //Put the extra free block back to free list.

        return block_ptr;
    }
    else{
        put(block_ptr,pack(block_allocating,1));
        put((uint64_t*)((char*)block_ptr + block_allocating - footer_size),pack(block_allocating,1));
        return block_ptr;
        //Left free block is smaller than minimum block size, So just allocate it without split.
    }
}

uint64_t* expand_heap(uint64_t new_block_size){
    void* new_ptr = mm_sbrk(new_block_size);
    if(new_ptr ==(void*) -1){
        return NULL;
    }
    
    uint64_t* newblock_header = heap_epi;
    *newblock_header = (new_block_size) | 0;         //Header of new block
    *(uint64_t *)((char*)newblock_header + new_block_size - footer_size) = (new_block_size) | 0;    //Footer of new block
    heap_epi = (uint64_t*)((char*)newblock_header + new_block_size);
    *heap_epi = 0x0000000000000000 | 0x0000000000000001;        //Reset the epilogue at the end of heap.
    return newblock_header;
}

node_t* find_firstfit_in_free_list(uint64_t size){
    int freelist_array_index = go_which_range_freelist(size);

    for (int i = freelist_array_index; i < free_list_num; i++){
        node_t* current_block = freelist_heads[i];
        if (current_block == NULL){     
            continue;      
            //If this free list is empty, try next bigger freelist. 
        }
        else{
            //if the free list is not empty, find suitable free block in free list.
            while(current_block != NULL){
                if (get_total_block_size(get_header_ptr((uint64_t*)current_block)) >= size){
                    return current_block;
                }
                current_block = current_block->next;
            }
        }
    }
    return NULL;
}



/*
 * malloc
 */
void* malloc(size_t size)
{
    // IMPLEMENT THIS FROM HINT
    if (size == 0){return NULL;}
    if (size>=16){size = size;}else{size = 16;}
    // Check the valid and minimum size, min size is 32, payload minimum is 16.

    uint64_t total_block_size =(uint64_t)align(size+header_size+footer_size);     
    uint64_t* current_ptr;

    // Explicit find fit and allocate:
    node_t* find_ptr = find_firstfit_in_free_list(total_block_size);
    if (find_ptr == NULL){
        current_ptr = expand_heap(total_block_size);
        //dbg_printf("1malloc1 size %ld and aligned size is %ld at %p\n", size, total_block_size, current_ptr);
        uint64_t* after_allocated_current_ptr = split_and_allocate_block(current_ptr,total_block_size);
        return get_payload_ptr(after_allocated_current_ptr);
    }
    else{
        //dbg_printf("2found in freelist malloc size %ld and aligned size is %ld at %p\n", size, total_block_size, get_header_ptr((uint64_t*)find_ptr));
        //dbg_printf("2Freelist_head store at %p and next is %p, prev is %p\n", find_ptr, freelist_heads[go_which_range_freelist(total_block_size)]->next, freelist_heads[go_which_range_freelist(total_block_size)]->prev);
        remove_from_freelist((uint64_t*)find_ptr,get_total_block_size(get_header_ptr((uint64_t*)find_ptr)));
        //dbg_printf("2malloc size %ld and aligned size is %ld at %p\n", size, total_block_size, get_header_ptr((uint64_t*)find_ptr));
        uint64_t* after_allocated_current_ptr = split_and_allocate_block(get_header_ptr((uint64_t*)find_ptr),total_block_size);
        return get_payload_ptr(after_allocated_current_ptr);
    }
}

// Idea from textbook chapter 9.

uint64_t* merge(uint64_t* block_ptr){
    uint64_t* prev_block_footer = (uint64_t*)((char*)block_ptr - footer_size);
    uint64_t* prev_block_header = (uint64_t*)((char*)block_ptr - get_total_block_size(prev_block_footer));
    uint64_t* next_block_header = get_next_block(block_ptr);
    uint64_t prev_status = is_block_allocated(prev_block_footer);
    uint64_t next_status = is_block_allocated(next_block_header);
    uint64_t total_size = get_total_block_size(block_ptr);

    if (prev_status == 1 && next_status == 1){
        add_to_freelist(get_payload_ptr(block_ptr),get_total_block_size(block_ptr));
        return block_ptr;
    }
    else if(prev_status == 1 && next_status == 0){
        remove_from_freelist(get_payload_ptr(next_block_header),get_total_block_size(next_block_header));

        total_size += get_total_block_size(next_block_header);
        put(block_ptr,pack(total_size,0));
        put((uint64_t*)((char*)get_next_block(block_ptr) - footer_size),pack(total_size,0));

        add_to_freelist(get_payload_ptr(block_ptr),get_total_block_size(block_ptr));
        //|-curr header--payload--footer-|-next header--payload--footer-|
        //|------------Merge1------------|------------Merge2------------| 
        //Total size is the total size of 2 free block will be merged here.
        //first put changed current block header size and its status
        //second put, because we already changed the whole merge block header, so we can use this approach to get the pointer to footer's beginning.
    }
    else if(prev_status == 0 && next_status == 1){
        remove_from_freelist(get_payload_ptr(prev_block_header),get_total_block_size(prev_block_header));

        total_size += get_total_block_size(prev_block_footer);
        put((uint64_t*)((char*)get_next_block(block_ptr) - footer_size),pack(total_size,0));
        put((uint64_t*)((char*)block_ptr - get_total_block_size(prev_block_footer)),pack(total_size,0));
        //|-prev header--payload--footer-|-curr header--payload--footer-|
        //|                              |                              |
        //|                              here is block_ptr              here is get_next_block(block_ptr)
        //here is block_ptr - get_total_block_size(prev_block_footer)
        block_ptr = (uint64_t*)((char*)block_ptr - get_total_block_size(prev_block_footer));

        add_to_freelist(get_payload_ptr(block_ptr),total_size);
    }
    else{
        remove_from_freelist(get_payload_ptr(next_block_header),get_total_block_size(next_block_header));
        remove_from_freelist(get_payload_ptr(prev_block_header),get_total_block_size(prev_block_header));

        total_size += get_total_block_size(prev_block_footer) + get_total_block_size(next_block_header);

        uint64_t* prev_header = (uint64_t*)((char*)block_ptr - get_total_block_size(prev_block_footer));
        put(prev_header,pack(total_size,0)); //put whole merge block header its size and its status to merge 1 header 

        uint64_t* next_footer = (uint64_t*)((char*)prev_header + get_total_block_size(prev_header) - footer_size);
        put(next_footer,pack(total_size,0)); //put whole merge block header its size and its status to merge 3 footer

        //|-prev header--payload--footer-|-curr header--payload--footer-|-next header--payload--footer-|
        //|------------Merge1------------|------------Merge2------------|------------Merge3------------|  

        block_ptr = (uint64_t*)((char*)block_ptr - get_total_block_size(prev_block_footer));

        add_to_freelist(get_payload_ptr(block_ptr),get_total_block_size(block_ptr));
    }
    return block_ptr;
}

/*
 * free
 */

void free(void* ptr)
{
    // IMPLEMENT THIS
    if (ptr == NULL){
        return;
    }
    uint64_t* block_ptr = (uint64_t*) ptr - 1;  //Get block_ptr point to block beginning;

    //dbg_printf("3free payload at %p and header at %p\n", ptr, block_ptr);

    uint64_t whole_size = get_total_block_size(block_ptr);
    put(block_ptr,pack(whole_size,0));
    uint64_t payload_size = whole_size - header_size - footer_size;
    uint64_t* block_footer_ptr = (uint64_t*)((char*)block_ptr + payload_size + header_size); 
    put(block_footer_ptr,pack(whole_size,0));
    //Free block header and footer setting.

    //dbg_printf("3Freelist store at %p and next is %p, prev is %p\n", ptr, freelist_heads[go_which_range_freelist(whole_size)]->next, freelist_heads[go_which_range_freelist(whole_size)]->prev);
    merge(block_ptr);

    mm_checkheap(__LINE__);
}


/*
 * realloc
 */
void* realloc(void* oldptr, size_t size)
{
    // IMPLEMENT THIS
    // printf("realloc size %p at %ld\n",oldptr, size);
    if(oldptr == NULL){
        return malloc(size);
    }
    if(size == 0){
        free(oldptr);
        return 0;
    }

    uint64_t* blcok_ptr = (uint64_t*) ((char*)oldptr - header_size); //uint64 8bytes
    uint64_t current_payload_size = get_total_block_size(blcok_ptr) - header_size - footer_size;
    //because the size info is in header, so get blcok_ptr to header beginning

    if (current_payload_size == size){
        return oldptr;
    }
    //if equal, no need to move

    size_t keep_size;
    if(size > current_payload_size){
        keep_size = current_payload_size;
    }
    else{
        keep_size = size;
    }
    
    //it's min(new_size, old_size), new size is in parameter of the realloc, and old_size got by get_total_block_size helper func

    void* newptr = malloc(size);
    if(newptr == (void*)-1){
        return 0;
    }
    mm_memcpy(newptr,oldptr,keep_size);    //move the data, these 2 ptr both pointer to the beginning of payload.
    free(oldptr);
    return newptr;
}

/*
 * calloc
 * This function is not tested by mdriver, and has been implemented for you.
 */
void* calloc(size_t nmemb, size_t size)
{
    void* ptr;
    size *= nmemb;
    ptr = malloc(size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

/*
 * Returns whether the pointer is in the heap.
 * May be useful for debugging.
 */
static bool in_heap(const void* p)
{
    return p <= mm_heap_hi() && p >= mm_heap_lo();
}

/*
 * Returns whether the pointer is aligned.
 * May be useful for debugging.
 */
static bool aligned(const void* p)
{
    size_t ip = (size_t) p;
    return align(ip) == ip;
}

/*
 * mm_checkheap
 * You call the function via mm_checkheap(__LINE__)
 * The line number can be used to print the line number of the calling
 * function where there was an invalid heap.
 */
bool mm_checkheap(int line_number)
{
#ifdef DEBUG
    // Write code to check heap invariants here
    // IMPLEMENT THIS

    //heap checker
    uint64_t* checker_ptr;    //Where the heap start
    for(checker_ptr = heap_pre; get_total_block_size(checker_ptr) > 0; checker_ptr = get_next_block(checker_ptr)){
        assert(*checker_ptr == *(get_next_block(checker_ptr)- 1));
        //Checking whole heap that the block header and footer's consistency

        if (is_block_allocated(checker_ptr) == 0 && is_block_allocated(get_next_block(checker_ptr)) == 0){
            printf("Block nearby free, but no merge, block at %p in line %d\n",checker_ptr,line_number);
            return false;
        }
        //checking there are no 2 free block not merge.
        
        if (is_block_allocated(checker_ptr) == 0){
            uint64_t* payload_ptr = get_payload_ptr(checker_ptr);
            for (int i = 0; i < free_list_num; i ++){
                node_t* current = freelist_heads[i];
                while (current != NULL){
                    if ((uint64_t*)current == payload_ptr){
                        return true;
                    }
                    current = current->next;
                }
            }
            printf("Free Block can not find in freelist. block at %p in line %d\n",checker_ptr,line_number);
            return false;
        }
        //checking all the free block is in free list.
    }


    //freelist array checker
    for (int i = 0; i < free_list_num; i ++){
        node_t* current = freelist_heads[i];
        while (current != NULL){
            if(current->next != NULL){
                if(current->next->prev != current){
                    printf("Block in freelist prev and next ptr is not ok, block at %p in line %d\n",get_header_ptr((uint64_t*)current),line_number);
                    return false;
                }
            }
            if(is_block_allocated(get_header_ptr((uint64_t*)current)) != 0){
                printf("Block in freelist, but its header not set to free, block at %p in line %d\n",get_header_ptr((uint64_t*)current),line_number);
                return false;
            }
            if(go_which_range_freelist(get_total_block_size(get_header_ptr((uint64_t*)current))) != i){
                printf("Block is in wrong interval freelist, block at %p in line %d\n",get_header_ptr((uint64_t*)current),line_number);
                return false;
            }
            if(in_heap((uint64_t*)current) == false){
                printf("Block is not in heap, block at %p in line %d\n",get_header_ptr((uint64_t*)current),line_number);
                return false;
            }
            current = current->next;
        }
    }

#endif // DEBUG
    return true;
}