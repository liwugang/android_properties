
#include <string>
#include <vector>

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

using namespace std;

class property_info;

// Copy from AOSP
struct PropertyInfoAreaHeader {
  // The current version of this data as created by property service.
  uint32_t current_version;
  // The lowest version of libc that can properly parse this data.
  uint32_t minimum_supported_version;
  uint32_t size;
  uint32_t contexts_offset;
  uint32_t types_offset;
  uint32_t root_offset;
};

// Copy from AOSP
struct TrieNodeInternal {
  // This points to a property entry struct, which includes the name for this node
  uint32_t property_entry;

  // Children are a sorted list of child nodes_; binary search them.
  uint32_t num_child_nodes;
  uint32_t child_nodes;

  // Prefixes are terminating prefix matches at this node, sorted longest to smallest
  // Take the first match sequentially found with StartsWith().
  uint32_t num_prefixes;
  uint32_t prefix_entries;

  // Exact matches are a sorted list of exact matches at this node_; binary search them.
  uint32_t num_exact_matches;
  uint32_t exact_match_entries;
};

struct property_entry {
    string name;
    int context_index;
    int type_index;
};

uint32_t read_u32(uint8_t **pos);

class property_node {
    public:
        void init(property_info *info, uint8_t *begin, uint32_t offset);
        void read_property_entry(property_entry &entry, uint8_t *begin, uint32_t offset);

        property_entry &get_entry() { return entry_; }
        vector<property_entry> &get_prefixes() { return prefixes_; }
        vector<property_entry> &get_exact_matches() { return exact_matches_; }
        vector<property_node> &get_children() { return children_; }

    private:
        TrieNodeInternal *node_;
        property_entry entry_;
        vector<property_node> children_;
        vector<property_entry> prefixes_;
        vector<property_entry> exact_matches_;

    private:
        property_info *info_;
};

class property_info {
    public:
        property_info();
        ~property_info();

        uint32_t get_context_size() { return context_offset_.size(); }
        string get_context(uint32_t index);
        string get_type(uint32_t index);
        string get_context(const char *property_name);
        void print();
        void print(property_node &node);
        bool is_valid();

    private:
        bool read_from_file();
        void check_prefix_match(const char* remaining_name, property_node& trie_node,
                                uint32_t* context_index, uint32_t* type_index);

    private:
        uint8_t *property_info_data_;
        uint32_t property_info_length_;
        vector<uint32_t> context_offset_;
        vector<uint32_t> type_offset_;
        property_node root_;
};

