# PelotonHashTable
Implementations of hash tables for CMUDB/peloton to validate a series of assumptions and implementations

There are currently three implementations in this repo: 

HashTable_OA_KVL: Open addressing with Key-Value-List to hold duplicated values for the same key
HashTable_CA_CC: Closed addressing with collision chain as collision resolution strategy
HashTable_CA_SCC: Closed addressing with collision chain, but unlike the previous one, it does not chain all buckets together for easiness of deleting entries (so this hash table does not support removal, but it is faster)
