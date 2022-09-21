// a simple single-file implementation file system.
// gabriel campbell
// 2022-08-21

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

uint8_t* disk;			

#define DISK_CAPACITY 512000000
#define INODES_CAPACITY 512
#define MAX_FRAGMENTS 1			/* maximum number of sections a node can be split up into; fragmentation not supported yet so this should be 1 */
#define METADATA_SIZE (44+(MAX_FRAGMENTS*16))		/* 32 bytes for filename, u64 for data size, u32 for num data sections */

/*	=> the disk starts with fs info:
		u32 n_max_inodes			- how many inodes the fs has
		u32 n_active_inodes			- how many inodes are active
		u32 n_deleted_inodes		- how many inodes have been deleted

	=> then comes the inodes, followed by all of the node data
	=> an inode is:
		u8 type
		u64 address					- node metadata
	=> node data is of 2 parts:
		1. metadata (addressed by inode)
			-----
			32 bytes filename
			u64 size					- total size of node data
			u32 num data sections
			u64 data section address + length pairs
		2. data sections (not necessarily following metadata - use metadata's section addresses)

	INODE TYPES:
	0 - deleted
	1 - file
	2 - directory
*/

#define FS_INFO_SIZE 12
#define INODE_SIZE 9

uint32_t get_inodes_capacity() {
	return *(uint32_t*)disk;
}

uint32_t get_num_active_inodes() {
	return *(uint32_t*)(disk + 4);
}

uint32_t get_num_deleted_inodes() {
	return *(uint32_t*)(disk + 8);
}

uint64_t get_node_size(uint32_t node) {
	uint64_t metadata_address = *(uint64_t*)(disk + FS_INFO_SIZE + node * INODE_SIZE + 1);
	return *(uint64_t*)(disk + metadata_address + 32);
}

void read_node(uint32_t node, uint8_t* data, uint64_t n_bytes) {
	if(!n_bytes || n_bytes > get_node_size(node))
		return;
	uint64_t metadata_address = *(uint64_t*)(disk + FS_INFO_SIZE + node * INODE_SIZE + 1);
	uint64_t data_address = *(uint64_t*)(disk + metadata_address + 44);
	memmove(data, &disk[data_address], n_bytes);
}

void write_node(uint32_t node, uint8_t* data, uint64_t n_bytes) {
	if(!n_bytes || n_bytes > get_node_size(node))
		return;
	uint64_t metadata_address = *(uint64_t*)(disk + FS_INFO_SIZE + node * INODE_SIZE + 1);
	uint64_t data_address = *(uint64_t*)(disk + metadata_address + 44);
	memmove(&disk[data_address], data, n_bytes);
}




// find first active but deleted inode on fs
uint32_t find_first_deleted_inode() {
	uint32_t inode_cap = get_inodes_capacity();
	for(uint32_t i = 0; i < inode_cap; i++)
		if(disk[FS_INFO_SIZE + i * INODE_SIZE] == 0) {
			*(uint32_t*)(disk + 8) -= 1;	// decrease number of deleted inodes
			return i;
		}
}

// add a new inode to the inode table and return its ID, or -1 on fail
int32_t new_inode() {
	if(get_num_deleted_inodes()) {		// restore a deleted inode
		uint32_t inode = find_first_deleted_inode();
		return inode;
	}
	if(get_num_active_inodes() + 1 > get_inodes_capacity())
		return -1;
	*(uint32_t*)(disk + 4) += 1;			// increment num active inodes
	return get_num_active_inodes() - 1;
}

void delete_inode(uint32_t inode) {
	disk[FS_INFO_SIZE + inode * INODE_SIZE] = 0;
	*(uint32_t*)(disk + 8) += 1;	// increase number of deleted inodes
}

// check for node metadata/data overlap with address range min -> max. returns 1 on overlap, 0 otherwise.
uint8_t check_overlap(uint32_t node, uint64_t min_address, uint64_t max_address) {
	uint64_t metadata_address = *(uint64_t*)(disk + FS_INFO_SIZE + node * INODE_SIZE + 1);
	if(max_address >= metadata_address && metadata_address + METADATA_SIZE - 1 >= min_address)
		return 1;		// node metadata collides with address range
	uint32_t section_count = *(uint32_t*)(disk + metadata_address + 40);
	uint64_t* sections = (uint64_t*)(disk + metadata_address + 44);			// data section address + length pairs
	for(uint32_t i = 0; i < section_count; i++)
		if(max_address >= sections[i * 2] && sections[i * 2] + sections[i * 2 + 1] - 1 >= min_address)
			return 1;
	return 0;
}

uint64_t find_node_max_address(uint32_t node) {
	uint64_t metadata_address = *(uint64_t*)(disk + FS_INFO_SIZE + node * INODE_SIZE + 1);
	uint64_t max_address = metadata_address + METADATA_SIZE - 1;
	uint32_t section_count = *(uint32_t*)(disk + metadata_address + 40);
	uint64_t* sections = (uint64_t*)(disk + metadata_address + 44);			// data section address + length pairs
	for(uint32_t i = 0; i < section_count; i++)
		if(sections[i * 2] + sections[i * 2 + 1] - 1 > max_address)
			max_address = sections[i * 2] + sections[i * 2 + 1] - 1;
	return max_address;
}

uint64_t locate_space(uint64_t size) {
	uint64_t min_addr = FS_INFO_SIZE + get_inodes_capacity() * INODE_SIZE;
	uint64_t max_addr = min_addr + size - 1;
	uint32_t n_active_inodes = get_num_active_inodes();
	uint8_t available = !n_active_inodes;

	for(uint32_t i = 0; i < n_active_inodes; i++) {
		if(disk[FS_INFO_SIZE + i * INODE_SIZE] == 0) continue;		// node i is deleted
		min_addr = find_node_max_address(i) + 1;
		max_addr = min_addr + size - 1;
		uint8_t overlap = 0;
		for(uint32_t j = 0; j < n_active_inodes; j++) {
			if(disk[FS_INFO_SIZE + j * INODE_SIZE] == 0 || i == j)
				continue;
			if(check_overlap(j, min_addr, max_addr)) {
				overlap = 1;
				break;
			}
		}
		if(!overlap) {		// found available position; no need to continue search
			available = 1;
			break;
		}
	}
	return available && max_addr < DISK_CAPACITY ? min_addr : 0;
}

uint32_t resize_node(uint32_t inode, uint64_t new_size);

void add_child_node(uint32_t parent_node, uint32_t child_node) {
	if(disk[FS_INFO_SIZE + parent_node * INODE_SIZE] != 2)
		return;	// parent node must be a directory
	if(resize_node(parent_node, get_node_size(parent_node) + 4) != 0)
		return;	// no space on disk for new entry in node's parent directory

	uint32_t node_size = get_node_size(parent_node);

	uint32_t* list = calloc(1, node_size);
	read_node(parent_node, (uint8_t*)list, node_size);
	list[node_size / 4 - 1] = child_node;
	write_node(parent_node, (uint8_t*)list, node_size);
	free(list);
}

void remove_child_node(uint32_t parent_node, uint32_t child_node) {
	if(disk[FS_INFO_SIZE + parent_node * INODE_SIZE] != 2)
		return;	// parent node must be a directory

	uint32_t node_size = get_node_size(parent_node);
	uint32_t* list = calloc(1, node_size);
	read_node(parent_node, (uint8_t*)list, node_size);

	uint32_t n_entries = node_size / 4, found = 0;
	for(uint32_t i = 0; i < n_entries; i++)
		if(list[i] == child_node) {
			list[i] = list[n_entries - 1];
			found = 1;
			break;
		}

	if(!found) return;	// dir entry for child not found
	write_node(parent_node, (uint8_t*)list, node_size);
	resize_node(parent_node, node_size - 4);
}

// add a new file/directory and its metadata, return file ID (inode) or -1 on fail
int32_t new_node(uint8_t* name, uint32_t data_size, uint32_t parent_node, uint8_t node_type) {
	if(strlen(name) > 32 || !node_type) return -1;
	for(char c = 0; c < strlen(name); c++)
		if(name[c] <= 0x1F || name[c] >= 0x7F || name[c] == ':' || name[c] == '|' || name[c] == '\\' || name[c] == '/'
		||  name[c] == '*' || name[c] == '?'  || name[c] == '"' || name[c] == '<' || name[c] == '>')
			return -1;

	// find space for new node

	uint64_t metadata_address = locate_space(METADATA_SIZE + data_size);
	if(!metadata_address) return -1;		// no space for node data on disk

	// get new inode

	int32_t inode = new_inode();
	if(inode == -1) return -1;

	// update inode info

	uint8_t* iptr = &disk[FS_INFO_SIZE + inode * INODE_SIZE];
	*iptr = node_type;
	*(uint64_t*)(iptr + 1) = metadata_address;			// set inode to address of the start of node's metadata 

	// write new node's metadata

	memset(&disk[metadata_address], 0, 32);
	memcpy(&disk[metadata_address], name, strlen(name));
	*(uint64_t*)(disk + metadata_address + 32) = data_size;
	*(uint32_t*)(disk + metadata_address + 40) = data_size > 0;
	*(uint64_t*)(disk + metadata_address + 44) = metadata_address + METADATA_SIZE;		// set node's data section address
	*(uint64_t*)(disk + metadata_address + 52) = data_size;

	if(inode)		// skip for root dir init (don't add root dir as child of root dir)
		add_child_node(parent_node,inode);

	return inode;
}

// delete a file/directory and its metadata
void delete_node(uint32_t parent_node, uint32_t node) {
	if(!node) return;			// cannot delete root dir node

	remove_child_node(parent_node, node);
	delete_inode(node);
}

// returns 0 on success, 1 on failure
uint32_t resize_node(uint32_t inode, uint64_t new_size) {
	// get address of the inode's metadata.
	uint64_t metadata_address = *(uint64_t*)(disk + FS_INFO_SIZE + inode * INODE_SIZE + 1);
	uint64_t* node_size			= (uint64_t*)(disk + metadata_address + 32);
	uint32_t* num_sections		= (uint32_t*)(disk + metadata_address + 40);
	uint64_t* section_address	= (uint64_t*)(disk + metadata_address + 44);
	uint64_t* section_length	= (uint64_t*)(disk + metadata_address + 52);

	if(new_size == *node_size)
		return 0;

	if(!new_size) {			// size of 0 = no data sections
		*node_size = 0;		// set node size to 0
		*num_sections = 0;	// set num data sections to 0
		return 0;
	}

	if(!*node_size) {	// original has no data sections
		uint64_t address = locate_space(new_size);
		if(!address) return 1;		// resize failed - not enough space on disk for resized file

		*node_size = new_size;		// set node size
		*num_sections = 1;			// set num of data sections
		*section_address = address;	// set data section address
		*section_length = new_size;	// set data section length
		return 0;
	}

	if(new_size < *node_size) {		// truncation
		*node_size = new_size;		// set node size
		*section_length = new_size;	// set data section length
	} else if(new_size > *node_size) {		// expand file with 0s
		uint64_t address = locate_space(new_size);
		if(!address)
			return 1;		// resize failed - not enough space on disk to create a resized copy of file

		// copy data from old address to the new address

		memmove(&disk[address], &disk[*section_address], *node_size);
		memset(&disk[address] + *node_size, 0, new_size - *node_size);

		// update the node's metadata

		*node_size = new_size;
		*section_address = address;
		*section_length = new_size;
	}
	return 0;
}

// move a file or directory
void move_node(uint32_t node, uint32_t parent_node, uint32_t dst_dir) {
	remove_child_node(parent_node, node);
	add_child_node(dst_dir, node);
}

void init_disk() {
	disk = calloc(1, DISK_CAPACITY);
	*(uint32_t*)disk = INODES_CAPACITY;				// set max number of inodes

	// create the root directory
	int32_t root_node = new_node("", 0, 0, 2);		// inode 0
}


int main() {
	init_disk();
}
