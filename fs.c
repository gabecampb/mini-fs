// a simple single-file implementation file system.
// gabriel campbell
// 2022-08-21

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

uint8_t* disk;			
uint64_t disk_size = 512000000;		// 512 MB disk

void init_disk() {
	disk = calloc(1, disk_size);
}

//				DISK SECTIONS (from start to end)
//
// inodes table (fixed count)
// 		max (summed) number of files and directories
//		each inode is 9 bytes - this is address in disk to filename + size (file metadata), then file sections, which is followed by file data.
//		each directory is a file that lists all inodes (files/dirs) contained within it - everything is a file.
// 		inodes must be at fixed locations since directories will list inode indices.
// node data (file contents)

uint32_t n_max_inodes = 512;
uint8_t* used_inode_states;		// for each of n_used_inodes, 0 for existing inodes and 1 for deleted inodes
uint32_t n_used_inodes;
uint32_t* deleted_inodes;		// list of all currently deleted (unused) inodes
uint32_t n_deleted_inodes;
uint32_t inode_width = 9;

uint32_t metadata_width = 33;	// 32 bytes for filename, 1 byte for node type (directory/file)

uint64_t* used_regions;
uint64_t* used_region_lengths;
uint32_t n_used_regions;



// add a new inode to the inode table and return its ID, or -1 on fail
// reuse a deleted inode if applicable
int32_t new_inode() {
	if(n_deleted_inodes > 1) {
		int32_t inode = deleted_inodes[n_deleted_inodes-1];
		deleted_inodes = realloc(deleted_inodes, sizeof(uint32_t) * (n_deleted_inodes - 1));
		used_inode_states[inode] = 0;	// restored inode; mark as existing
		n_deleted_inodes--;
		return inode;
	} else if(n_deleted_inodes == 1) {
		int32_t inode = deleted_inodes[0];
		free(deleted_inodes);
		deleted_inodes = 0;
		used_inode_states[inode] = 0;	// restored inode; mark as existing
		n_deleted_inodes--;
		return inode;
	}

	if(n_used_inodes + 1 > n_max_inodes) return -1;
	used_inode_states = realloc(used_inode_states, sizeof(uint8_t) * (n_used_inodes + 1));
	used_inode_states[n_used_inodes] = 0;	// mark inode as existing
	return n_used_inodes++;
}

// mark an inode in the inode table as deleted, and remove associated node from used_regions
void delete_inode(uint32_t inode) {
	if(inode >= n_used_inodes) return;
	for(uint32_t i = 0; i < n_used_regions; i++)
		if(used_regions[i] == *(uint64_t*)&disk[inode * inode_width + 1]) {
			// remove used_regions[i] and used_region_lengths[i]
			used_regions[i] = used_regions[n_used_regions - 1];
			used_region_lengths[i] = used_region_lengths[n_used_regions - 1];
			used_regions = realloc(used_regions, sizeof(uint64_t) * (n_used_regions - 1));
			used_region_lengths = realloc(used_region_lengths, sizeof(uint64_t) * (n_used_regions - 1));
			n_used_regions--;
		}
	used_inode_states[inode] = 1;	// mark inode as deleted
	deleted_inodes = realloc(deleted_inodes, sizeof(uint32_t) * (n_deleted_inodes + 1));
	deleted_inodes[n_deleted_inodes] = inode;
	n_deleted_inodes++;
}

// name should be null-terminated and with a max length of 32.
void write_metadata(uint64_t address, uint8_t* name, uint64_t node_size) {
	memcpy(&disk[address], name, strlen(name));
	*(uint64_t*)(disk + address + 32) = node_size;
}

// add a new file/directory and its metadata, return file ID (inode) or -1 on fail
int32_t new_node(uint8_t* name, uint32_t n_bytes_data, uint32_t parent_inode, uint8_t node_type) {
	if(strlen(name) > 32) return -1;	// node name is too long
	// find location of new file (no fragmentation yet)
	uint64_t min_address = n_max_inodes * inode_width;
	uint64_t max_address = min_address + metadata_width + n_bytes_data - 1;
	if(max_address >= disk_size) return -1;
	for(uint32_t i = 0; i < n_used_regions; i++)
		if(max_address >= used_regions[i] && used_regions[i] + used_region_lengths[i] >= min_address) {
			// found collision between existing node data and new node data, try checking for space after the existing node data
			min_address = used_regions[i] + used_region_lengths[i];
			max_address = min_address + metadata_width + n_bytes_data - 1;
			if(max_address >= disk_size) return -1;
		}

	// get new inode
	int32_t inode = new_inode();
	if(inode == -1) return -1;
	
	// update inode info
	uint8_t* iptr = &disk[inode * inode_width];
	*iptr = node_type;
	*(uint64_t*)(iptr + 1) = min_address;

	// add min_address, max_address to used_regions.
	used_regions = realloc(used_regions, sizeof(uint64_t) * (n_used_regions + 1));
	used_region_lengths = realloc(used_region_lengths, sizeof(uint64_t) * (n_used_regions + 1));
	used_regions[n_used_regions] = min_address;
	used_region_lengths[n_used_regions] = metadata_width + n_bytes_data;
	n_used_regions++;

	// write node's metadata
	write_metadata(min_address, name, metadata_width + n_bytes_data);

	return inode;
}




int main() {
	init_disk();
}
