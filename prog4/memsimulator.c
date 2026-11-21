#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define PAGE_SIZE 256
#define PAGE_COUNT 256
#define TLB_SIZE 16

typedef enum {
    fifo,
    lru,
    opt
} PRA;

//one page-table entry tells us which frame contains this page
typedef struct {
    int frame_number;
    bool valid; //true means the page is currently in physical memory
} PageTableEntry;

typedef struct {
    PageTableEntry entries[PAGE_COUNT];
} PageTable;

//one TLB entry stores one page-to-frame translation
typedef struct {
    int page_num;
    int frame_num;
    bool valid;
} TLBEntry;

typedef struct {
    TLBEntry entries[TLB_SIZE];
    int next_slot; //could do pointer or just index of next tlb entry
} TLB;

//backing store holds contents of virtual pages
typedef struct {
    FILE *file;
} BackingStore;

typedef struct {
    signed char bytes[PAGE_COUNT][PAGE_SIZE];
    //tracks which page occupies each frame
    int frame_page[PAGE_COUNT];
    //tracks when each frame was last accessed, used for LRU
    size_t last_used[PAGE_COUNT];

    int num_frames; //number of frames allowed for this run
    int frame_idx;
    int next_fifo; //next physical frame to replace using FIFO
} PhysicalMemory;

typedef struct {
    unsigned int *addresses;
    size_t count; //number of addresses read
    size_t capacity; //number of addresses the allocated array can hold
} ReferenceSequence;

//bits 8-15 is the page number
unsigned int get_pagenum(unsigned int address){
    return (address >> 8) & 0xFF;
}

//bits 0-7 is the offset
unsigned int get_pageoffset(unsigned int address){
    return address & 0xFF;
}

//read all addresses first so OPT can look at future references
int read_addresses(FILE *input, ReferenceSequence *sequence){
    unsigned int address;
    int result;

    while ((result = fscanf(input, "%u", &address)) == 1) {
        //make array larger when runs out of space
        if (sequence->count == sequence->capacity) {
            size_t new_capacity;

            if (sequence->capacity == 0) {
                new_capacity = 1024;
            } else {
                new_capacity = sequence->capacity * 2;
            }

            //use a temporary pointer so failed realloc does not lose the array
            unsigned int *temp = realloc(sequence->addresses, new_capacity * sizeof(unsigned int));
            if (temp == NULL) {
                printf("Could not allocate the address array\n");
                return -1;
            }

            sequence->addresses = temp;
            sequence->capacity = new_capacity;
        }

        sequence->addresses[sequence->count] = address;
        sequence->count++;
    }

    //EOF is expected; invalid text or a file error is not
    if (result != EOF || ferror(input)) {
        printf("Could not read the addresses\n");
        return -1;
    }

    return 0;
}

//prepare physical memory before translating any addresses
void initialize_memory(PhysicalMemory *memory, int frames){
    memset(memory, 0, sizeof(*memory));
    memory->num_frames = frames;

    for (int i = 0; i < PAGE_COUNT; i++) {
        memory->frame_page[i] = -1; //this frame does not contain a page yet
    }
}

//return the frame number on a TLB hit, or -1 on a miss
int lookup_TLB(TLB *tlb, int page_num){
    for (int i = 0; i < TLB_SIZE; i++) {
        if (tlb->entries[i].valid && tlb->entries[i].page_num == page_num) {
            return tlb->entries[i].frame_num;
        }
    }

    return -1;
}

//the TLB always uses rotating FIFO, regardless of the selected PRA
void insert_TLB(TLB *tlb, int page_num, int frame_num){
    int slot = tlb->next_slot;
    tlb->entries[slot].page_num = page_num;
    tlb->entries[slot].frame_num = frame_num;
    tlb->entries[slot].valid = true;

    //wrap back to slot 0 after inserting into slot 15
    tlb->next_slot = (slot + 1) % TLB_SIZE;
}

//remove a translation when its page is evicted from physical memory
void invalidate_TLB(TLB *tlb, int page_num){
    for (int i = 0; i < TLB_SIZE; i++) {
        if (tlb->entries[i].valid && tlb->entries[i].page_num == page_num) {
            tlb->entries[i].valid = false;
        }
    }

    //do not move next_slot or shift entries when invalidating a slot
}

//return the resident frame number, or -1 if the page is absent
int lookup_page_table(PageTable *page_table, int page_num){
    if (page_table->entries[page_num].valid) {
        return page_table->entries[page_num].frame_number;
    }

    return -1;
}

//copy one page from the backing store into the selected frame
int read_backing_store(BackingStore *backing, PhysicalMemory *memory, int page_num, int frame_num){
    //the file position depends on the logical PAGE, not the physical frame
    long position = (long)page_num * PAGE_SIZE;

    if (fseek(backing->file, position, SEEK_SET) != 0) {
        printf("Could not seek to page %d\n", page_num);
        return -1;
    }

    size_t bytes_read = fread(memory->bytes[frame_num], 1, PAGE_SIZE, backing->file);

    if (bytes_read != PAGE_SIZE) {
        printf("Could not read page %d\n", page_num);
        return -1;
    }

    return 0;
}

//choose a free frame or select a frame to replace
int choose_frame(PhysicalMemory *memory, ReferenceSequence *sequence, PRA pra, size_t current){
    //use free frames before replacing any existing pages
    if (memory->frame_idx < memory->num_frames) {
        int frame_num = memory->frame_idx;
        memory->frame_idx++;
        return frame_num;
    }

    if (pra == fifo) {
        int victim = memory->next_fifo;
        //cycle through the physical frames in loading order
        memory->next_fifo = (memory->next_fifo + 1) % memory->num_frames;
        return victim;
    }

    int victim = 0;

    if (pra == lru) {
        //the smallest last_used value belongs to the least recently used frame
        for (int frame = 1; frame < memory->num_frames; frame++) {
            if (memory->last_used[frame] < memory->last_used[victim]) {
                victim = frame;
            }
        }

    } else if (pra == opt) {
        size_t farthest = 0;

        for (int frame = 0; frame < memory->num_frames; frame++) {
            int loaded_page = memory->frame_page[frame];
            size_t next = current + 1;

            //look ahead until this page is referenced again
            while (next < sequence->count) {
                int future_page = get_pagenum(sequence->addresses[next]);

                if (future_page == loaded_page) {
                    break;
                }

                next++;
            }

            //a page never referenced again is a good eviction choice
            if (next == sequence->count) {
                return frame;
            }

            //otherwise choose the page whose next reference is farthest away
            if (next > farthest) {
                farthest = next;
                victim = frame;
            }
        }
    }

    return victim;
}

//handle a page fault and return the frame containing the requested page
int load_page(PageTable *page_table, TLB *tlb, PhysicalMemory *memory, BackingStore *backing, ReferenceSequence *sequence, PRA pra, int page_num, size_t current){
    int frame_num = choose_frame(memory, sequence, pra, current);

    if (read_backing_store(backing, memory, page_num, frame_num) != 0) {
        return -1;
    }

    int old_page = memory->frame_page[frame_num];

    //if the frame was occupied, remove the old page's mappings
    if (old_page != -1) {
        page_table->entries[old_page].valid = false;
        invalidate_TLB(tlb, old_page);
    }

    //record the new page-to-frame mapping
    memory->frame_page[frame_num] = page_num;
    page_table->entries[page_num].frame_number = frame_num;
    page_table->entries[page_num].valid = true;

    return frame_num;
}

//translate one address and print its result
int translate(PageTable *page_table, TLB *tlb, PhysicalMemory *memory, BackingStore *backing, ReferenceSequence *sequence, PRA pra, size_t current, int *page_faults, int *tlb_hits){
    unsigned int address = sequence->addresses[current];

    int page_num = get_pagenum(address);
    int offset = get_pageoffset(address);

    //first try to find the translation in the TLB
    int frame_num = lookup_TLB(tlb, page_num);

    if (frame_num != -1) {
        (*tlb_hits)++;
    } else {
        //a TLB miss does not necessarily mean a page fault
        frame_num = lookup_page_table(page_table, page_num);

        if (frame_num == -1) {
            //page isnt in physical memory, so load
            frame_num = load_page(page_table, tlb, memory, backing, sequence, pra, page_num, current);
            if (frame_num == -1) {
                return -1;
            }

            (*page_faults)++;
        }

        //cache the translation after resolving the TLB miss
        insert_TLB(tlb, page_num, frame_num);
    }

    //update LRU information on every access
    memory->last_used[frame_num] = current;

    //the offset stays the same when converting to a physical address
    int physical_address = frame_num * PAGE_SIZE + offset;

    int physical_frame = physical_address / PAGE_SIZE;
    int physical_offset = physical_address % PAGE_SIZE;

    int value = memory->bytes[physical_frame][physical_offset];

    //match the reference format: address, signed value, frame, page dump
    printf("%u, %d, %d, ", address, value, frame_num);

    //print entire frame as uppercase hexadecimal without spaces
    for (int i = 0; i < PAGE_SIZE; i++) { //cast as unsigned char b/c have as signed and then to int
        printf("%02X", (int)(unsigned char)memory->bytes[frame_num][i]);
    }
    printf("\n"); //new line after frame for same formatting as address-correct.txt

    return 0;
}

int main(int argc, char *argv[]){
    int frames = PAGE_COUNT;
    PRA pra = fifo;

    //need filename at least other args optional
    if (argc < 2 || argc > 4) {
        printf("Usage: <reference-file> [FRAMES] [fifo|lru|opt]\n");
        return -1;
    }

    if (argc >= 3) {
        frames = atoi(argv[2]);

        if (frames <= 0 || frames > PAGE_COUNT) {
            printf("Frames must be between 1 and 256\n");
            return -1;
        }
    }
    
    if (argc == 4) {
        if (strcmp(argv[3], "fifo") == 0) {
            pra = fifo;
        } else if (strcmp(argv[3], "lru") == 0) {
            pra = lru;
        } else if (strcmp(argv[3], "opt") == 0) {
            pra = opt;
        } else {
            printf("PRA must be fifo, lru, or opt\n");
            return -1;
        }
    }

    FILE *input = fopen(argv[1], "r");
    if (input == NULL) {
        printf("Could not open the address file\n");
        return -1;
    }

    ReferenceSequence sequence = {0};

    int result = read_addresses(input, &sequence);
    fclose(input);

    if (result != 0) {
        free(sequence.addresses);
        return -1;
    }

    BackingStore backing;
    backing.file = fopen("BACKING_STORE.bin", "rb");
    if (backing.file == NULL) {
        printf("Could not open BACKING_STORE.bin\n");
        free(sequence.addresses);
        return -1;
    }

    //zero initialization makes every page-table and TLB entry invalid
    PageTable page_table = {0};
    TLB tlb = {0};

    PhysicalMemory memory;
    initialize_memory(&memory, frames);

    int page_faults = 0;
    int tlb_hits = 0;

    //translate each address in the order it appeared in the input
    for (size_t i = 0; i < sequence.count; i++) {
        result = translate(&page_table, &tlb, &memory, &backing, &sequence, pra, i, &page_faults, &tlb_hits);

        if (result != 0) {
            break;
        }
    }

    if (result == 0) {
        double page_fault_rate = 0;
        double tlb_hit_rate = 0;

        //avoid dividing by zero if the address file is empty
        if (sequence.count > 0) {
            page_fault_rate = (double)page_faults / sequence.count;
            tlb_hit_rate = (double)tlb_hits / sequence.count;
        }

        //print fractions to match addresses-correct.txt
        printf("Number of Translated Addresses = %zu\n", sequence.count);
        printf("Page Faults = %d\n", page_faults);
        printf("Page Fault Rate = %.3f\n", page_fault_rate); //3 decimal points
        printf("TLB Hits = %d\n", tlb_hits);
        printf("TLB Misses = %zu\n", sequence.count - tlb_hits);
        printf("TLB Hit Rate = %.3f\n", tlb_hit_rate);
    }

    fclose(backing.file);
    free(sequence.addresses);

    if (result != 0) {
        return -1;
    }

    return 0;
}