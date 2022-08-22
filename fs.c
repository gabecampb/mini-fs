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

#define MAX_FRAGMENTS 1			/* maximum number of sections a node can be split up into */

void init_disk() {
	disk = calloc(1, DISK_CAPACITY);

	*(uint32_t*)disk = INODES_CAPACITY;				// set max number of inodes
}

#define MAX_FRAGMENTS 1			/* maximum number of sections a node can be split up into; fragmentation not supported yet so this should be 1 */
#define METADATA_SIZE 44+(MAX_FRAGMENTS*16)		/* 32 bytes for filename, u32 for data size, u32 for num data sections */

/*	=> the disk starts with fs info:
		u32 n_max_inodes			- how many inodes the fs has
		u32 n_active_inodes			- how many inodes are active
		u32 n_deleted_inodes		- how many inodes have been deleted

	=> then comes the inodes, followed by all of the node data
	=> an inode is:
		u8 type
		u64 address					- node metadata
	=> node data is:
		32 bytes filename
		u64 size					- total size of node data
		u32 num data sections
		u64 data section address + length pairs
		node data

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

// locate a space in the node data region. returns address to space or 0 on fail.
uint64_t locate_space(uint64_t size) {
	uint64_t min_address = FS_INFO_SIZE + get_inodes_capacity() * INODE_SIZE;
	uint64_t max_address = min_address + size - 1;

	uint32_t n_active_inodes = get_num_active_inodes();
	for(uint32_t i = 0; i < n_active_inodes; i++) {
		if(disk[FS_INFO_SIZE + i * INODE_SIZE] == 0)
			continue;		// if current inode is deleted, continue (it could not overlap with the new node data)

		// check if this inode's node metadata overlaps with range min_address to max_address

		uint64_t metadata_address = *(uint64_t*)(disk + FS_INFO_SIZE + i * INODE_SIZE + 1);

		if(max_address >= metadata_address && metadata_address + METADATA_SIZE - 1 >= min_address) {
			min_address = metadata_address + METADATA_SIZE;
			max_address = min_address + size - 1;
		}

		// check if this inode's node data sections overlap with range min_address to max_address

		uint32_t section_count = *(uint32_t*)(disk + metadata_address + 40);
		uint64_t* sections = (uint64_t*)(disk + metadata_address + 44);			// data section address + length pairs
		uint8_t has_overlap = 0;
		uint64_t max_address_section = 0;
		for(uint32_t j = 0; j < section_count; j++) {
			if(sections[j * 2] > max_address_section)
				max_address_section = j;
			if(max_address >= sections[j * 2] && sections[j * 2] + sections[j * 2 + 1] - 1 >= min_address)
				has_overlap = 1;
		}
		if(has_overlap) {		// collision found between new node and this inode's node data, set a new range
			min_address = sections[max_address_section * 2] + sections[max_address_section * 2 + 1];
			max_address = min_address + size - 1;
		}
	}
	if(max_address >= DISK_CAPACITY)
		return 0;

	return min_address;
}

// add a new file/directory and its metadata, return file ID (inode) or -1 on fail
int32_t new_node(uint8_t* name, uint32_t data_size, uint32_t parent_inode, uint8_t node_type) {
	if(strlen(name) > 32 || !node_type) return -1;

	uint64_t metadata_address = locate_space(METADATA_SIZE + data_size);
	if(!metadata_address) return -1;		// no space for node data on disk

	uint64_t min_address = metadata_address + METADATA_SIZE;		// node data
	uint64_t max_address = min_address + data_size - 1;

	// get new inode

	int32_t inode = new_inode();
	if(inode == -1) return -1;

	// update inode info

	uint8_t* iptr = &disk[FS_INFO_SIZE + inode * INODE_SIZE];
	*iptr = node_type;
	*(uint64_t*)(iptr + 1) = metadata_address;			// set inode to address of the start of node's metadata 

	// write new node's metadata

	memcpy(&disk[metadata_address], name, strlen(name));
	*(uint64_t*)(disk + metadata_address + 32) = data_size;
	*(uint32_t*)(disk + metadata_address + 40) = data_size > 0;
	*(uint64_t*)(disk + metadata_address + 44) = min_address;
	*(uint64_t*)(disk + metadata_address + 52) = data_size;

	return inode;
}




int main() {
	init_disk();
}
