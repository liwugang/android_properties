
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>

#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>

#include <android/log.h>
#include <sys/system_properties.h>

#define PROP_NAME_MAX  32
#define PROP_VALUE_MAX 92

#define AREA_SIZE         (128 * 1024)
#define AREA_DATA_SIZE     (AREA_SIZE - sizeof(prop_area))

#define ANDROID_N   24
#define ANDROID_O   26

#define LOG_TYPE_CONSOLE	1
#define LOG_TYPE_LOGCAT 	2
#define LOG_BUFFER	1024

#define ALIGN(x, alignment)   ((x) + (sizeof(alignment) - 1) & ~(sizeof(alignment) -1))

#define LOG_TAG "properties"
#define LOGD(...)  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#define PROPERTIES_FILE  "/dev/__properties__"

/** 属性前缀 */
typedef struct prefix_node {
    char name[PROP_NAME_MAX];
    struct context_node *context;
    struct prefix_node *next;
} prefix_node;

/** 属性对应的security context */
typedef struct context_node {
    char name[PROP_VALUE_MAX];
    void *mem;
    struct context_node *next;
} context_node;

typedef struct prop_bt {
    uint8_t namelen;
    uint8_t reserved[3];
    uint32_t prop;
    uint32_t left;
    uint32_t right;
    uint32_t children;
    char name[0];
} prop_bt;

/** 保存属性 key value */
typedef struct prop_info {
    uint32_t serial;
    char value[PROP_VALUE_MAX];
    char name[0];
} prop_info;

typedef struct prop_area {
    uint32_t bytes_used;
    uint32_t serial;
    uint32_t magic;
    uint32_t version;
    uint32_t reserved[28];
    char data[0];
} prop_area;

int g_log_type = LOG_TYPE_CONSOLE + LOG_TYPE_LOGCAT; // 默认输出到logcat和console
bool g_need_security_context = false;
char *g_current_security_context = NULL;

prefix_node *g_prefixs = NULL;
context_node *g_contexts = NULL;

void print_log(const char *format, ...) {
	char buffer[LOG_BUFFER];
	va_list args;
	va_start(args, format);
	vsprintf(buffer, format, args);
	if ((g_log_type & LOG_TYPE_LOGCAT) != 0) {
		LOGD("%s", buffer);
	}
	if ((g_log_type & LOG_TYPE_CONSOLE) != 0) {
		printf("%s", buffer);
	}
	va_end(args);
}

void add_prefix_node(prefix_node *node) {
    if (g_prefixs == NULL) {
        g_prefixs = node;
        return;
    }
    int len = strlen(node->name);
    prefix_node **pp = NULL;
    for (pp = &g_prefixs; *pp != NULL; pp = &((*pp)->next)) {
        if (strlen((*pp)->name) < len || !strcmp((*pp)->name, "*")) {
            node->next = *pp;
            *pp = node;
            return;
        }
    }
    *pp = node;
}

prefix_node *get_prefix_node(const char *prop_name) {
    if (g_prefixs == NULL) {
        return NULL;
    }
    for (prefix_node *node = g_prefixs; node != NULL; node = node->next) {
        if (!strncmp(node->name, prop_name, strlen(node->name)) || !strcmp(node->name, "*")) {
            return node;
        }
    }
    return NULL;
}

void add_context_node(context_node *node) {
    if (g_contexts == NULL) {
        g_contexts = node;
        return;
    }
    node->next = g_contexts;
    g_contexts = node;
}

context_node *get_context_node(const char *context_name) {
    if (g_contexts == NULL) {
        return NULL;
    }
    for (context_node *node = g_contexts; node != NULL; node = node->next) {
        if (!strcmp(node->name, context_name)) {
            return node;
        }
    }
    return NULL;
}

bool initialize_contexts(const char *context_file) {
    FILE *file = fopen(context_file, "r");
    if (!file) {
        return false;
    }
    char *buffer = NULL;
    char *p = NULL;
    char *prop_prefix = NULL;
    char *prop_context = NULL;
    size_t len = 0;
    while (getline(&buffer, &len, file) > 0) {
        p = buffer;
        while (isspace(*p))
            p++;
        if (*p == '#' || *p == '\0') {
            continue;
        }
        prop_prefix = p;
        while (!isspace(*p) && *p != '\0') {
            p++;
        }
        prefix_node *p_prefix = (prefix_node *) malloc(sizeof(prefix_node));
        strncpy(p_prefix->name, prop_prefix, p - prop_prefix);
        p_prefix->context = NULL;
        p_prefix->next = NULL;

        while (isspace(*p))
            p++;
        if (*p == '\0') {
            free(p_prefix);
            continue;
        }
        prop_context = p;
        while (!isspace(*p) && *p != '\0') {
            p++;
        }
        *p = '\0';
        context_node *p_context = get_context_node(prop_context);
        if (p_context == NULL) {
            p_context = (context_node *) malloc(sizeof(context_node));
            strcpy(p_context->name, prop_context);
            p_context->mem = NULL;
            p_context->next = NULL;
            add_context_node(p_context);
        }
        p_prefix->context = p_context;
        add_prefix_node(p_prefix);
    }

    free(buffer);
    fclose(file);
    return true;
}

    void cleanup_resource() {
        prefix_node *p = g_prefixs;
        while (p != NULL) {
            p = p->next;
        }
        context_node *q = g_contexts;
        while (q != NULL) {
            q = q->next;
        }
    }

prop_area *map_prop_area(const char *file_name, bool need_write) {
    int open_flag;
    int map_prot_flag;
    if (need_write) {
        open_flag = O_RDWR;
        map_prot_flag = PROT_READ | PROT_WRITE;
    } else {
        open_flag = O_RDONLY;
        map_prot_flag = PROT_READ;
    }
    int fd = open(file_name, open_flag);
    if (fd == -1) {
        if (errno == EACCES) {
            fprintf(stderr, "open file[%s] error[%d]:%s\n", file_name, errno, strerror(errno));
        }
        return NULL;
    }
    struct stat fd_stat;
    if(fstat(fd, &fd_stat) < 0) {
        perror("cannot get stat:");
        close(fd);
        return NULL;
    }
    if (fd_stat.st_size != AREA_SIZE) {
        print_log("file [%s] size is not equal %x\n", file_name, AREA_SIZE);
        close(fd);
        return NULL;
    }
    void *addr = mmap(NULL, fd_stat.st_size, map_prot_flag, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        perror("map failed!");
        close(fd);
        return NULL;
    }
    close(fd);
    return (prop_area*) addr;
}

prop_bt *get_prop_bt(prop_area *p_area, uint32_t off) {
    if (off > AREA_DATA_SIZE) {
        fprintf(stderr, "exceed the limit\n");
        return NULL;
    }
    return (prop_bt *)(p_area->data + off);
}

prop_bt *new_prop_bt(prop_area *p_area, const char *name, uint8_t namelen, uint32_t *off) {
    uint32_t need_size = ALIGN(sizeof(prop_bt) + namelen + 1, sizeof(uint32_t));
    if (p_area->bytes_used + need_size > AREA_DATA_SIZE) {
        fprintf(stderr, "no enough space, total:[%u] used:[%u], need:[%u]\n", AREA_DATA_SIZE,
                p_area->bytes_used, need_size);
        return NULL;
    }
    *off = p_area->bytes_used;
    p_area->bytes_used += need_size;
    prop_bt* bt = (prop_bt *)(p_area->data + *off);
    memset(bt, 0, sizeof(prop_bt));
    bt->namelen = namelen;
    memcpy(bt->name, name, namelen);
    bt->name[namelen] = '\0';
    return bt;
}

prop_info *get_prop_info(prop_area *p_area, uint32_t off) {
    if (off > AREA_DATA_SIZE) {
        fprintf(stderr, "exceed the limit\n");
        return NULL;
    }
    prop_info *result = (prop_info *)(p_area->data + off);
	return result;
}

prop_info *new_prop_info(prop_area *p_area, const char *prop_name, uint8_t namelen, uint32_t *off) {
    uint32_t need_size = ALIGN(sizeof(prop_info) + namelen + 1, sizeof(uint32_t));
    if (p_area->bytes_used + need_size > AREA_DATA_SIZE) {
        fprintf(stderr, "no enough space, total:[%u] used:[%u], need:[%u]\n", AREA_DATA_SIZE,
                p_area->bytes_used, need_size);
        return NULL;
    }
    *off = p_area->bytes_used;
    p_area->bytes_used += need_size;
    prop_info* info = (prop_info *)(p_area->data + *off);
    memset(info, 0, sizeof(prop_info));
    memcpy(info->name, prop_name, namelen);
    info->name[namelen] = '\0';
    return info;
}

int get_sdk_version();

char *get_security_context(char *prop_name) {
    if (g_current_security_context != NULL) {
        return g_current_security_context;
    }

    /**
     * below Android N, ingnore "ro." prefix in the property_contexts file
     */
    if (get_sdk_version() < ANDROID_N) {
        char *name = strstr(prop_name, "ro.");
        if (name != NULL && strlen(name) == strlen(prop_name)) {
            prop_name = prop_name + strlen("ro.");
        }
    }
    prefix_node *p_prefix = get_prefix_node(prop_name);
    if (p_prefix == NULL || p_prefix->context == NULL) {
        return NULL;
    } else {
        return p_prefix->context->name;
    }
}

void recursive(prop_area *p_area, uint32_t off) {
    prop_bt *p_bt = get_prop_bt(p_area, off);
    if (p_bt == NULL) {
        return;
    }
    if (p_bt->prop != 0) {
        prop_info *p_info = get_prop_info(p_area, p_bt->prop);
        if (p_info != NULL) {
            print_log("[%s]: [%s]", p_info->name, p_info->value);
            if (g_need_security_context) {
                print_log(" [%s]", get_security_context(p_info->name));
            }
            print_log("\n");
        }
    }
    if (p_bt->left != 0) {
        recursive(p_area, p_bt->left);
    }
    if (p_bt->right != 0) {
        recursive(p_area, p_bt->right);
    }
    if (p_bt->children != 0) {
        recursive(p_area, p_bt->children);
    }
}

bool dump_properties_from_file(const char *file_name) {
    prop_area *p_area = map_prop_area(file_name, false);
    if (p_area == NULL) {
        return false;
    }
    recursive(p_area, 0);

    return true;
}

int get_sdk_version() {
    static int sdk_version = 0;
    if (sdk_version != 0) {
        return sdk_version;
    }
	char sdk_value[PROP_VALUE_MAX] = {0};
	__system_property_get("ro.build.version.sdk", sdk_value);
	if (strlen(sdk_value) > 0) {
        sdk_version = atoi(sdk_value);
    }
    return sdk_version;
}

/** 
 * 打印所有属性
 *  Android N之间所有属性是在/dev/__properties__文件中
 *  Android N上，每个security context对应一个文件，security context和属性前缀对应关系保存在/property_contexts文件中
 */
void dump_all() {
    if (get_sdk_version() < ANDROID_N) {
        dump_properties_from_file(PROPERTIES_FILE);
    } else {
        for (context_node *p_context = g_contexts; p_context != NULL; p_context = p_context->next) {
            char context_file[128] = PROPERTIES_FILE;
            strcat(context_file, "/");
            strcat(context_file, p_context->name);
            if (g_need_security_context) {
                g_current_security_context = (char *)p_context->name;
            }
            dump_properties_from_file(context_file);
        }
    }
}

int cmp_prop_name(const char *one, uint8_t one_len, const char *two, uint8_t two_len)
{
if (one_len < two_len)
    return -1;
else if (one_len > two_len)
    return 1;
else
    return strncmp(one, two, one_len);
}

prop_info *find_prop_info(prop_area *area, const char *prop_name, bool need_add) {
    if (area == NULL || strlen(prop_name) == 0) {
        return NULL;
    }
    prop_bt *prev_bt = get_prop_bt(area, 0);
    if (prev_bt->children == 0) {
        return NULL;
    }
    prop_bt *p_bt = get_prop_bt(area, prev_bt->children);
    const char *remain_name = prop_name;
    while (true) {
        const char *seq = strchr(remain_name, '.');
        bool want_subtree = (seq != NULL);
        uint8_t substr_size = want_subtree ? (seq - remain_name) : strlen(remain_name);

        if (p_bt == NULL && need_add) {
            p_bt = new_prop_bt(area, remain_name, substr_size, &prev_bt->children);
        }

        prop_bt *current = NULL;
        while (p_bt != NULL) {
            int ret = cmp_prop_name(remain_name, substr_size, p_bt->name, p_bt->namelen);
            if (ret == 0) {
                current = p_bt;
                break;
            } else if (ret < 0) {
                if (p_bt->left == 0) {
                    if (need_add) {
                        p_bt = new_prop_bt(area, remain_name, substr_size, &p_bt->left);
                    } else {
                        p_bt = NULL;
                    }
                } else {
                    p_bt = get_prop_bt(area, p_bt->left);
                }
            } else {
                if (p_bt->right == 0) {
                    if (need_add) {
                        p_bt = new_prop_bt(area, remain_name, substr_size, &p_bt->right);
                    } else {
                        p_bt = NULL;
                    }
                } else {
                    p_bt = get_prop_bt(area, p_bt->right);
                }
            }
        }
        if (current != NULL) {
            if (!want_subtree) {
                prop_info *info = NULL;
                if (current->prop == 0) {
                    if (need_add) {
                        info = new_prop_info(area, prop_name, strlen(prop_name), &current->prop);
                    } else {
                        info = NULL;
                    }
                } else {
                    info = get_prop_info(area, current->prop);
                }
                return info;
            } else {
                remain_name = seq + 1;
                if (current->children == 0) {
                    p_bt = NULL;
                } else {
                    p_bt = get_prop_bt(area, current->children);
                }
                prev_bt = current;
            }
        } else {
            return NULL;
        }
    }
    return NULL;
}

void get_or_set_property_value(const char *prop_name, const char *prop_value) {
    prop_area *p_area = NULL;
    if (get_sdk_version() < ANDROID_N) {
        p_area = map_prop_area(PROPERTIES_FILE, prop_value != NULL);
    } else {
        prefix_node *p_prefix = get_prefix_node(prop_name);
        if (p_prefix == NULL || p_prefix->context == NULL) {
            fprintf(stderr, "can't find security context file!\n");
            return;
        }
        char context_file[128] = PROPERTIES_FILE;
        strcat(context_file, "/");
        strcat(context_file, p_prefix->context->name);
        if (g_need_security_context) {
            g_current_security_context = p_prefix->context->name;
        }
        p_area = map_prop_area(context_file, prop_value != NULL);
    }
    prop_info *p_info = find_prop_info(p_area, prop_name, prop_value != NULL);
    if (p_info != NULL) {
        if (prop_value != NULL) {
            uint8_t valuelen = strlen(prop_value);
            memcpy(p_info->value, prop_value, valuelen);
            p_info->value[valuelen] = '\0';
            p_info->serial = (valuelen << 24) | (p_info->serial & 0xffffff);
            print_log("set %s == %s success\n", prop_name, prop_value);
        }
        print_log("[%s]:[%s]", p_info->name, p_info->value);
        if (g_need_security_context) {
            print_log(" [%s] ", get_security_context(p_info->name));
        }
        print_log("\n");
    }
}

static void usage() {
    fprintf(stderr,
        "usage: system_properties [-h] [-a] [-l log_level] [-s] prop_name prop_value\n"
        "  -h:                  display this help message\n"
        "  -a:                  dump all system properties\n"
        "  -l log_level:        console = 1 logcat = 2  consle + logcat = 3(default)\n"
        "  -s                   print security context(selabel)\n"
        );
}

int main(int argc, char *argv[]) {
    bool need_all = true;
    char *prop_name = NULL;
    char *prop_value = NULL;

    for (;;) {
        int option_index = 0;
        int ic = getopt(argc, argv, "hal:s");
        if (ic < 0) {
            if (optind < argc) {
                prop_name = argv[optind];
            }
            if (optind + 1 < argc) {
                prop_value = argv[optind + 1];
            }
            break;
        }
        switch(ic) {
            case 'h':
                usage();
                return -1;
            case 'a':
                need_all = true;
                break;
            case 'l':
                g_log_type = atoi(optarg);
                break;
            case 's':
                g_need_security_context = true;
                break;
            default:
                usage();
                return -1;
        }
    }


    if (prop_name != NULL) {
        if (strlen(prop_name) >= PROP_NAME_MAX) {
            fprintf(stderr, "prop_name[%s] is too long, need less %d\n", prop_name, PROP_NAME_MAX);
            return -1;
        }
        need_all = false;
    }
    if (prop_value != NULL) {
        if (strlen(prop_value) >= PROP_VALUE_MAX) {
            fprintf(stderr, "prop_value[%s] is too long, need less %d\n", prop_value, PROP_VALUE_MAX);
            return -1;
        }
        if (geteuid() != 0) {
            fprintf(stderr, "set property value need root first!\n");
            return -1;
        }
    }
    if (get_sdk_version() >= ANDROID_O) {
        if (access("/system/etc/selinux/plat_property_contexts", R_OK) != -1) {
            initialize_contexts("/system/etc/selinux/plat_property_contexts");
            initialize_contexts("/vendor/etc/selinux/nonplat_property_contexts");
            initialize_contexts("/vendor/etc/selinux/vendor_property_contexts"); // name changed in android P
        } else {
            initialize_contexts("/plat_property_contexts");
            initialize_contexts("/nonplat_property_contexts");
        }
    } else {
        initialize_contexts("/property_contexts");
    }
    if (need_all) {
        dump_all();
    } else {
        get_or_set_property_value(prop_name, prop_value);
    }
    cleanup_resource();
}
