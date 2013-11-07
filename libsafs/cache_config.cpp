/**
 * Copyright 2013 Da Zheng
 *
 * This file is part of SAFSlib.
 *
 * SAFSlib is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SAFSlib is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with SAFSlib.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <tr1/unordered_map>

#include "cache_config.h"
#include "associative_cache.h"
#if 0
#include "hash_index_cache.h"
#include "LRU2Q.h"
#endif
#include "NUMA_cache.h"
#include "file_mapper.h"

page_cache *cache_config::create_cache_on_node(int node_id,
		int max_num_pending_flush) const
{
	page_cache *cache;
	switch (get_type()) {
#if 0
		case LRU2Q_CACHE:
			cache = new LRU2Q_cache(get_part_size(node_id));
			break;
		case HASH_INDEX_CACHE:
			cache = new hash_index_cache(get_part_size(node_id), node_id);
			break;
#endif
		case ASSOCIATIVE_CACHE:
			cache = associative_cache::create(get_part_size(node_id),
					MAX_CACHE_SIZE, node_id, 1, max_num_pending_flush);
			break;
		default:
			fprintf(stderr, "wrong cache type\n");
			exit(1);
	}
	return cache;
}

void cache_config::destroy_cache_on_node(page_cache *cache) const
{
	switch (get_type()) {
#if 0
		case LRU2Q_CACHE:
		case HASH_INDEX_CACHE:
			delete cache;
			break;
#endif
		case ASSOCIATIVE_CACHE:
			associative_cache::destroy((associative_cache *) cache);
			break;
		default:
			fprintf(stderr, "wrong cache type\n");
			exit(1);
	}
}

page_cache *cache_config::create_cache(int max_num_pending_flush) const
{
	std::vector<int> node_ids;
	get_node_ids(node_ids);
	if (node_ids.size() == 1)
		return create_cache_on_node(node_ids[0], max_num_pending_flush);
	else
		return new NUMA_cache(this, max_num_pending_flush);
}

void cache_config::destroy_cache(page_cache *cache) const
{
	std::vector<int> node_ids;
	get_node_ids(node_ids);
	if (node_ids.size() == 1)
		destroy_cache_on_node(cache);
	else
		delete cache;
}

static bool node_exist(const std::vector<int> &node_ids, int node_id)
{
	for (size_t i = 0; i < node_ids.size(); i++)
		if (node_ids[i] == node_id)
			return true;
	return false;
}

file_map_cache_config::file_map_cache_config(long size, int type,
		const std::vector<int> &node_ids,
		file_mapper *mapper, int shift): cache_config(size, type)
{
	this->mapper = mapper;
	this->shift = shift;
	// This counts the number of files connected to each node.
	std::map<int, int> node_files;
	for (int i = 0; i < mapper->get_num_files(); i++) {
		int node_id = mapper->get_file_node_id(i);
		std::map<int, int>::iterator it = node_files.find(node_id);
		if (it == node_files.end())
			node_files.insert(std::pair<int, int>(node_id, 1));
		else
			it->second++;
	}

	std::tr1::unordered_map<int, long> part_sizes;
	int tot_files = 0;
	for (size_t i = 0; i < node_ids.size(); i++) {
		int node_id = node_ids[i];
		std::map<int, int>::const_iterator it = node_files.find(node_id);
		int new_node_id = (node_id + shift) % node_ids.size();
		if (it == node_files.end()) {
			part_sizes.insert(std::pair<int, long>(new_node_id, 0));
			printf("file mapping: cache part %d: size: %d\n", new_node_id, 0);
		}
		else {
			int num_files = it->second;
			tot_files += num_files;
			long part_size = size * (((float) num_files)
					/ mapper->get_num_files());
			assert(node_exist(node_ids, new_node_id));
			part_sizes.insert(std::pair<int, long>(new_node_id, part_size));
			printf("file mapping: cache part %d: size: %ld\n",
					new_node_id, part_size);
		}
	}
	assert(tot_files == mapper->get_num_files());
	init(part_sizes);
}

int file_map_cache_config::page2cache(off_t off) const
{
	int idx = mapper->map2file(off / PAGE_SIZE);
	return mapper->get_file_node_id(idx) + shift;
}