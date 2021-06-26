
#include "property_info.h"

uint32_t read_u32(uint8_t **pos) {
    uint32_t result = *(uint32_t *) (*pos);
    (*pos) += sizeof(uint32_t);
    return result;
}

bool property_info::is_valid() {
    return property_info_data_ != nullptr;
}

bool property_info::read_from_file() {
    int fd = open("/dev/__properties__/property_info", O_RDONLY);
    if (fd < 0) {
        property_info_data_ = NULL;
        return false;
    }
    struct stat st;
    fstat(fd, &st);
    property_info_length_ = st.st_size;
    property_info_data_ = (uint8_t *) mmap(NULL, property_info_length_, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (property_info_data_ == MAP_FAILED) {
        return false;
    }

    if (property_info_length_ < sizeof(PropertyInfoAreaHeader)) {
        return false;
    }

    PropertyInfoAreaHeader *header = (PropertyInfoAreaHeader *) property_info_data_;

    uint8_t *pos = property_info_data_ + header->contexts_offset;
    uint32_t context_len = read_u32(&pos);
    // printf("context len: %d\n", context_len);
    for (uint32_t i = 0; i < context_len; i++) {
        context_offset_.push_back(read_u32(&pos));
    }

    pos = property_info_data_ + header->types_offset;
    uint32_t type_len = read_u32(&pos);
    // printf("type len: %d\n", type_len);
    for (uint32_t i = 0; i < type_len; i++) {
        type_offset_.push_back(read_u32(&pos));
    }

    pos = property_info_data_ + header->root_offset;

    root_.init(this, property_info_data_, header->root_offset);
    return true;
}

property_info::property_info() {
    if (!read_from_file()) {
        property_info_data_ = nullptr;
        return;
    }
}

property_info::~property_info() {
    munmap(property_info_data_, property_info_length_);
}

string property_info::get_context(uint32_t index) {
    if (index >= context_offset_.size()) {
        return "";
    } else {
        return string((char *)(property_info_data_ + context_offset_[index]));
    }
}

string property_info::get_type(uint32_t index) {
    if (index >= type_offset_.size()) {
        return "";
    } else {
        return string((char *)(property_info_data_ + type_offset_[index]));
    }
}

void property_info::check_prefix_match(const char* remaining_name, property_node& trie_node,
                                uint32_t* context_index, uint32_t* type_index) {
    const uint32_t remaining_name_size = strlen(remaining_name);
    for (uint32_t i = 0; i < trie_node.get_prefixes().size(); ++i) {
        property_entry &entry = trie_node.get_prefixes().at(i);
        auto prefix_len = entry.name.size();
        if (prefix_len > remaining_name_size) continue;

        if (!strncmp(entry.name.c_str(), remaining_name, prefix_len)) {
            if (entry.context_index != ~0u) {
                *context_index = entry.context_index;
            }
            if (entry.type_index != ~0u) {
                *type_index = entry.type_index;
            }
            return;
        }
    }
}

void property_info::print(property_node &node) {
    printf("current: %s %d-%d-%d\n", node.get_entry().name.c_str(), node.get_prefixes().size(),
            node.get_exact_matches().size(), node.get_children().size());
    for (uint32_t i = 0; i < node.get_prefixes().size(); i++) {
        printf("\t - %d prefix: %s\n", i, node.get_prefixes().at(i).name.c_str());
    }
    for (uint32_t i = 0; i < node.get_exact_matches().size(); i++) {
        printf("\t - %d exact: %s\n", i, node.get_exact_matches().at(i).name.c_str());
    }
    for (uint32_t i = 0; i < node.get_children().size(); i++) {
        printf("\t - %d child: %s\n", i, node.get_children().at(i).get_entry().name.c_str());
    }
    for (uint32_t i = 0; i < node.get_children().size(); i++) {
        print(node.get_children().at(i));
    }
}

void property_info::print() {
    print(root_);
}

string property_info::get_context(const char *property_name) {
    uint32_t return_context_index = ~0u;
    uint32_t return_type_index = ~0u;
    const char* remaining_name = property_name;
    property_node trie_node = root_;
    while (true) {
        const char* sep = strchr(remaining_name, '.');

        // Apply prefix match for prefix deliminated with '.'
        if (trie_node.get_entry().context_index != ~0u) {
            return_context_index = trie_node.get_entry().context_index;
        }

        // Check prefixes at this node.  This comes after the node check since these prefixes are by
        // definition longer than the node itself.
        check_prefix_match(remaining_name, trie_node, &return_context_index, &return_type_index);

        if (sep == nullptr) {
          break;
        }

        const uint32_t substr_size = sep - remaining_name;
        auto children = trie_node.get_children();
        auto it = children.begin();
        for (; it != children.end(); it++) {
            string child_name = it->get_entry().name;
            if (!strncmp(child_name.c_str(), remaining_name, substr_size) && child_name.c_str()[substr_size] == '\0') {
                trie_node = *it;
                break;
            }
        }
        if (it == children.end()) {
            break;
        }

        remaining_name = sep + 1;
    }

    // We've made it to a leaf node, so check contents and return appropriately.
    // Check exact matches
    for (uint32_t i = 0; i < trie_node.get_exact_matches().size(); ++i) {
        if (!strcmp(trie_node.get_exact_matches().at(i).name.c_str(), remaining_name)) {
            property_entry entry = trie_node.get_exact_matches().at(i);
            if (entry.context_index != ~0u) {
                return get_context(entry.context_index);
            }
        }
    }

    // Check prefix matches for prefixes not deliminated with '.'
    check_prefix_match(remaining_name, trie_node, &return_context_index, &return_type_index);
    return get_context(return_context_index);
}

void property_node::init(property_info *info, uint8_t *begin, uint32_t offset) {
    info_ = info;
    node_ = (TrieNodeInternal *) (begin + offset);

    uint8_t *pos = begin + node_->property_entry;
    read_property_entry(entry_, begin, node_->property_entry);

    pos = begin + node_->prefix_entries;
    for (uint32_t i = 0; i < node_->num_prefixes; i++) {
        uint32_t off = read_u32(&pos);
        property_entry entry;
        read_property_entry(entry, begin, off);
        prefixes_.push_back(entry);
    }

    pos = begin + node_->exact_match_entries;
    for (uint32_t i = 0; i < node_->num_exact_matches; i++) {
        uint32_t off = read_u32(&pos);
        property_entry entry;
        read_property_entry(entry, begin, off);
        exact_matches_.push_back(entry);
    }

    pos = begin + node_->child_nodes;
    for (uint32_t i = 0; i < node_->num_child_nodes; i++) {
        property_node node;
        node.init(info, begin, read_u32(&pos));
        children_.push_back(node);
    }
}

void property_node::read_property_entry(property_entry &entry, uint8_t *begin, uint32_t offset) {
    uint8_t *pos = begin + offset;
    entry.name = string((char *)(begin + read_u32(&pos)), read_u32(&pos));
    entry.context_index = read_u32(&pos);
    entry.type_index = read_u32(&pos);
}
