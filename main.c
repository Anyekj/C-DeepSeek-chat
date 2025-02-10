#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <stdbool.h>

const char *api_key = "<这里填写你的api_key>";

typedef struct {
    char role[16];
    char content[51200];
} Message;

int deep_search_mode = 0;
int multi_line_mode = 0;

struct MemoryStruct {
    char *stream_buffer;
    size_t stream_size;
    char *assistant_reply;
    size_t reply_size;
    char *reasoning_content;
    size_t reasoning_size;
};

bool is_valid_utf8(const char *str) {
    while (*str) {
        if ((*str & 0x80) == 0) {
            str++;
        } else if ((*str & 0xE0) == 0xC0) {
            if ((str[1] & 0xC0) != 0x80) return false;
            str += 2;
        } else if ((*str & 0xF0) == 0xE0) {
            if ((str[1] & 0xC0) != 0x80 || (str[2] & 0xC0) != 0x80) return false;
            str += 3;
        } else if ((*str & 0xF8) == 0xF0) {
            if ((str[1] & 0xC0) != 0x80 || (str[2] & 0xC0) != 0x80 || (str[3] & 0xC0) != 0x80) return false;
            str += 4;
        } else {
            return false;
        }
    }
    return true;
}

void remove_invalid_utf8(char *str) {
    char *src = str;
    char *dst = str;

    while (*src) {
        if ((*src & 0x80) == 0) {
            *dst++ = *src++;
        } else if ((*src & 0xE0) == 0xC0) {
            if ((src[1] & 0xC0) == 0x80) {
                *dst++ = *src++;
                *dst++ = *src++;
            } else {
                src++;
            }
        } else if ((*src & 0xF0) == 0xE0) {
            if ((src[1] & 0xC0) == 0x80 && (src[2] & 0xC0) == 0x80) {
                *dst++ = *src++;
                *dst++ = *src++;
                *dst++ = *src++;
            } else {
                src++;
            }
        } else if ((*src & 0xF8) == 0xF0) {
            if ((src[1] & 0xC0) == 0x80 && (src[2] & 0xC0) == 0x80 && (src[3] & 0xC0) == 0x80) {
                *dst++ = *src++;
                *dst++ = *src++;
                *dst++ = *src++;
                *dst++ = *src++;
            } else {
                src++;
            }
        } else {
            src++;
        }
    }
    *dst = '\0';
}

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *new_stream = realloc(mem->stream_buffer, mem->stream_size + realsize + 1);
    if (!new_stream) {
        fprintf(stderr, "内存分配失败\n");
        return 0;
    }
    mem->stream_buffer = new_stream;
    memcpy(&mem->stream_buffer[mem->stream_size], contents, realsize);
    mem->stream_size += realsize;
    mem->stream_buffer[mem->stream_size] = '\0';

    char *line_start = mem->stream_buffer;
    while (1) {
        char *data_start = strstr(line_start, "data: ");
        if (!data_start) break;

        char *line_end = strchr(data_start, '\n');
        if (!line_end) break;

        *line_end = '\0';
        char *json_data = data_start + 6;

        char *rc_start = strstr(json_data, "\"reasoning_content\":\"");
        if (rc_start) {
            rc_start += strlen("\"reasoning_content\":\"");
            char *rc_end = strchr(rc_start, '"');
            if (rc_end) {
                *rc_end = '\0';

                char *unescaped = malloc(strlen(rc_start) + 1);
                if (!unescaped) {
                    fprintf(stderr, "内存分配失败\n");
                    return 0;
                }
                char *in = rc_start, *out = unescaped;
                while (*in) {
                    if (*in == '\\' && *(in+1) == 'n') {
                        *out++ = '\n';
                        in += 2;
                    } else {
                        *out++ = *in++;
                    }
                }
                *out = '\0';

                const char *new_reasoning = unescaped;
                size_t new_len = strlen(new_reasoning);
                const char *delta = new_reasoning;
                size_t delta_len = new_len;

                if (mem->reasoning_content != NULL) {
                    if (strncmp(new_reasoning, mem->reasoning_content, mem->reasoning_size) == 0) {
                        delta = new_reasoning + mem->reasoning_size;
                        delta_len = new_len - mem->reasoning_size;
                    }
                }

                if (delta_len > 0) {
                    if (mem->reasoning_size == 0) {
                        printf("\033[33m思考:[\033[0m");
                    }
                    printf("\033[33m%.*s\033[0m", (int)delta_len, delta);
                    fflush(stdout);
                }

                free(mem->reasoning_content);
                mem->reasoning_content = strdup(new_reasoning);
                mem->reasoning_size = new_len;

                free(unescaped);
                *rc_end = '"';
            }
        }

        char *c_start = strstr(json_data, "\"content\":\"");
        if (c_start) {
            c_start += strlen("\"content\":\"");
            char *c_end = strchr(c_start, '"');
            if (c_end) {
                *c_end = '\0';

                char *unescaped = malloc(strlen(c_start) + 1);
                if (!unescaped) {
                    fprintf(stderr, "内存分配失败\n");
                    return 0;
                }
                char *in = c_start, *out = unescaped;
                while (*in) {
                    if (*in == '\\' && *(in+1) == 'n') {
                        *out++ = '\n';
                        in += 2;
                    } else {
                        *out++ = *in++;
                    }
                }
                *out = '\0';

                const char *new_content = unescaped;
                size_t new_content_len = strlen(new_content);
                const char *content_delta = new_content;
                size_t content_delta_len = new_content_len;

                if (mem->assistant_reply != NULL) {
                    if (strncmp(new_content, mem->assistant_reply, mem->reply_size) == 0) {
                        content_delta = new_content + mem->reply_size;
                        content_delta_len = new_content_len - mem->reply_size;
                    }
                }

                if (content_delta_len > 0) {
                    if (deep_search_mode && mem->reply_size == 0) {
                        printf("\033[33m]\n\033[0m");
                    }
                    printf("\033[32m%.*s\033[0m", (int)content_delta_len, content_delta);
                    fflush(stdout);
                }

                free(mem->assistant_reply);
                mem->assistant_reply = strdup(new_content);
                mem->reply_size = new_content_len;

                free(unescaped);
                *c_end = '"';
            }
        }

        line_start = line_end + 1;
    }

    size_t processed = line_start - mem->stream_buffer;
    size_t remaining = mem->stream_size - processed;
    memmove(mem->stream_buffer, line_start, remaining);
    mem->stream_size = remaining;
    mem->stream_buffer[mem->stream_size] = '\0';

    return realsize;
}

char *json_escape(const char *input) {
    size_t len = strlen(input);
    char *output = malloc(len * 6 + 1);
    if (!output) {
        fprintf(stderr, "内存分配失败\n");
        return NULL;
    }
    char *q = output;
    const char *p = input;

    while (*p) {
        switch (*p) {
            case '"': strcpy(q, "\\\""); q += 2; break;
            case '\\': strcpy(q, "\\\\"); q += 2; break;
            case '\n': strcpy(q, "\\n"); q += 2; break;
            case '\r': strcpy(q, "\\r"); q += 2; break;
            case '\t': strcpy(q, "\\t"); q += 2; break;
            default: *q++ = *p; break;
        }
        p++;
    }
    *q = '\0';
    return output;
}

char *get_api_response(Message *messages, int message_count) {
    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk = {0};
    chunk.stream_buffer = malloc(1);
    if (!chunk.stream_buffer) {
        fprintf(stderr, "内存分配失败\n");
        return NULL;
    }
    chunk.stream_buffer[0] = '\0';
    chunk.assistant_reply = malloc(1);
    if (!chunk.assistant_reply) {
        fprintf(stderr, "内存分配失败\n");
        free(chunk.stream_buffer);
        return NULL;
    }
    chunk.assistant_reply[0] = '\0';
    chunk.reasoning_content = malloc(1);
    if (!chunk.reasoning_content) {
        fprintf(stderr, "内存分配失败\n");
        free(chunk.stream_buffer);
        free(chunk.assistant_reply);
        return NULL;
    }
    chunk.reasoning_content[0] = '\0';

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "CURL 初始化失败\n");
        free(chunk.stream_buffer);
        free(chunk.assistant_reply);
        free(chunk.reasoning_content);
        return NULL;
    }

    size_t json_len = 4096;
    char *json_str = malloc(json_len);
    if (!json_str) {
        fprintf(stderr, "内存分配失败\n");
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        free(chunk.stream_buffer);
        free(chunk.assistant_reply);
        free(chunk.reasoning_content);
        return NULL;
    }
    snprintf(json_str, json_len, "{\"model\": \"%s\", \"messages\": [",
             deep_search_mode ? "deepseek-reasoner" : "deepseek-chat");

    for (int i = 0; i < message_count; i++) {
        char *escaped = json_escape(messages[i].content);
        if (!escaped) {
            fprintf(stderr, "JSON 转义失败\n");
            free(json_str);
            curl_easy_cleanup(curl);
            curl_global_cleanup();
            free(chunk.stream_buffer);
            free(chunk.assistant_reply);
            free(chunk.reasoning_content);
            return NULL;
        }

        size_t needed = strlen(json_str) + strlen(escaped) + 256;
        if (needed > json_len) {
            json_len = needed * 2;
            char *new_json = realloc(json_str, json_len);
            if (!new_json) {
                fprintf(stderr, "内存分配失败\n");
                free(escaped);
                free(json_str);
                curl_easy_cleanup(curl);
                curl_global_cleanup();
                free(chunk.stream_buffer);
                free(chunk.assistant_reply);
                free(chunk.reasoning_content);
                return NULL;
            }
            json_str = new_json;
        }

        snprintf(json_str + strlen(json_str), json_len - strlen(json_str),
                 "{\"role\":\"%s\",\"content\":\"%s\"}%s",
                 messages[i].role, escaped, (i < message_count - 1) ? "," : "");
        free(escaped);
    }

    strcat(json_str, "], \"stream\": true}");

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (!headers) {
        fprintf(stderr, "CURL 头部信息添加失败\n");
        free(json_str);
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        free(chunk.stream_buffer);
        free(chunk.assistant_reply);
        free(chunk.reasoning_content);
        return NULL;
    }
    char auth[128];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", api_key);
    headers = curl_slist_append(headers, auth);
    if (!headers) {
        fprintf(stderr, "CURL 头部信息添加失败\n");
        curl_slist_free_all(headers);
        free(json_str);
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        free(chunk.stream_buffer);
        free(chunk.assistant_reply);
        free(chunk.reasoning_content);
        return NULL;
    }

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.deepseek.com/chat/completions");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "CURL 请求失败: %s\n", curl_easy_strerror(res));
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    free(json_str);
    free(chunk.stream_buffer);
    free(chunk.reasoning_content);

    return chunk.assistant_reply;
}



int main() {
    Message messages[100];
    int message_count = 0;
    char input[51200];

    printf("欢迎使用DeepSeek终端版\n");
    printf("作者：黯夜空間\n");
    printf("当前模式：%s（输入'模式切换'切换）\n", deep_search_mode ? "深度搜索" : "普通聊天");
    printf("当前输入模式：%s（输入'多行模式'切换）\n", multi_line_mode ? "多行输入" : "单行输入");

    while (1) {
        printf("\n我：");
        fflush(stdout);

        char input_buffer[51200] = {0};
        size_t input_len = 0;

        if (multi_line_mode) {
            int is_first_line = 1;
            while (1) {
                char line[51200];
                if (!fgets(line, sizeof(line), stdin)) break;
                line[strcspn(line, "\n")] = '\0';

                if (strcmp(line, "@_@") == 0) break;

                if (!is_first_line) {
                    if (input_len + 1 < sizeof(input_buffer)) {
                        input_buffer[input_len++] = '\n';
                    } else {
                        printf("输入过长，已截断！\n");
                        break;
                    }
                }

                size_t line_len = strlen(line);
                if (input_len + line_len < sizeof(input_buffer) - 1) {
                    strncpy(input_buffer + input_len, line, sizeof(input_buffer) - input_len - 1);
                    input_len += line_len;
                } else {
                    printf("输入过长，已截断！\n");
                    break;
                }

                is_first_line = 0;
                printf("... ");
                fflush(stdout);
            }
        } else {
            if (!fgets(input_buffer, sizeof(input_buffer), stdin)) break;
            input_buffer[strcspn(input_buffer, "\n")] = '\0';
            input_len = strlen(input_buffer);
        }

        if (input_len == 0) {
            printf("输入不能为空！\n");
            continue;
        }

        remove_invalid_utf8(input_buffer);
        input_len = strlen(input_buffer);
        if (input_len == 0) {
            printf("输入无效，请重新输入！\n");
            continue;
        }

        strncpy(input, input_buffer, sizeof(input) - 1);
        input[sizeof(input) - 1] = '\0';

        if (strcmp(input, "模式切换") == 0) {
            deep_search_mode = !deep_search_mode;
            printf("已切换到%s模式\n", deep_search_mode ? "深度搜索" : "普通聊天");
            continue;
        }
        if (strcmp(input, "多行模式") == 0) {
            multi_line_mode = !multi_line_mode;
            printf("已切换到%s模式\n", multi_line_mode ? "多行输入" : "单行输入");
            if (multi_line_mode) printf("结尾符：@_@\n");
            continue;
        }
        if (strcmp(input, "退出") == 0) break;

        strncpy(messages[message_count].role, "user", sizeof(messages[message_count].role) - 1);
        messages[message_count].role[sizeof(messages[message_count].role) - 1] = '\0';
        strncpy(messages[message_count].content, input, sizeof(messages[message_count].content) - 1);
        messages[message_count].content[sizeof(messages[message_count].content) - 1] = '\0';
        message_count++;

        printf("\nDS：");
        fflush(stdout);
        char *reply = get_api_response(messages, message_count);
        if (reply) {
            strncpy(messages[message_count].role, "assistant", sizeof(messages[message_count].role) - 1);
            messages[message_count].role[sizeof(messages[message_count].role) - 1] = '\0';
            strncpy(messages[message_count].content, reply, sizeof(messages[message_count].content) - 1);
            messages[message_count].content[sizeof(messages[message_count].content) - 1] = '\0';
            message_count++;
            free(reply);
        } else {
            printf("获取API回复失败\n");
        }
        printf("\n");
    }

    printf("对话结束，谢谢使用！\n");
    return 0;
}
