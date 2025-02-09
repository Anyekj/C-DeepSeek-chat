#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

const char *api_key = "<这里填写你的api_key>";

typedef struct {
    char role[16];
    char content[10240];
} Message;

int deep_search_mode = 0;

struct MemoryStruct {
    char *stream_buffer;
    size_t stream_size;
    char *assistant_reply;
    size_t reply_size;
    char *reasoning_content;
    size_t reasoning_size;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *new_stream = realloc(mem->stream_buffer, mem->stream_size + realsize + 1);
    if (!new_stream) return 0;
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

        // 处理 reasoning_content
        char *rc_start = strstr(json_data, "\"reasoning_content\":\"");
        if (rc_start) {
            rc_start += strlen("\"reasoning_content\":\"");
            char *rc_end = strchr(rc_start, '"');
            if (rc_end) {
                *rc_end = '\0';

                // Unescape 处理
                char *unescaped = malloc(strlen(rc_start) + 1);
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

                // 计算新增部分
                const char *new_reasoning = unescaped;
                size_t new_len = strlen(new_reasoning);
                const char *delta = new_reasoning;
                size_t delta_len = new_len;

                if (mem->reasoning_content != NULL) {
                    if (strncmp(new_reasoning, mem->reasoning_content, mem->reasoning_size) == 0) {
                        delta = new_reasoning + mem->reasoning_size;
                        delta_len = new_len - mem->reasoning_size;
                    } else {
                        delta = new_reasoning;
                        delta_len = new_len;
                    }
                }

                // 输出新增内容
                if (delta_len > 0) {
                    if (mem->reasoning_size == 0) { // 如果是第一次打印思考内容
                        printf("\033[33m思考:[\033[0m");
                    }
                    printf("\033[33m%.*s\033[0m", (int)delta_len, delta);
                    fflush(stdout);
                }

                // 更新存储的思考内容
                free(mem->reasoning_content);
                mem->reasoning_content = strdup(new_reasoning);
                mem->reasoning_size = new_len;

                free(unescaped);
                *rc_end = '"';
            }
        }

        // 处理最终回答 content
        char *c_start = strstr(json_data, "\"content\":\"");
        if (c_start) {
            c_start += strlen("\"content\":\"");
            char *c_end = strchr(c_start, '"');
            if (c_end) {
                *c_end = '\0';

                // Unescape 处理
                char *unescaped = malloc(strlen(c_start) + 1);
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

                // 计算新增部分
                const char *new_content = unescaped;
                size_t new_content_len = strlen(new_content);
                const char *content_delta = new_content;
                size_t content_delta_len = new_content_len;

                if (mem->assistant_reply != NULL) {
                    if (strncmp(new_content, mem->assistant_reply, mem->reply_size) == 0) {
                        content_delta = new_content + mem->reply_size;
                        content_delta_len = new_content_len - mem->reply_size;
                    } else {
                        content_delta = new_content;
                        content_delta_len = new_content_len;
                    }
                }

                // 输出新增内容
                if (content_delta_len > 0) {
                    if (deep_search_mode && mem->reply_size == 0) { // 如果是深度思考模式且是第一次打印回复内容
                        printf("\033[33m]\n\033[0m");
                    }
                    printf("\033[32m%.*s\033[0m", (int)content_delta_len, content_delta);
                    fflush(stdout);
                }

                // 更新存储的回复内容
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
    char *output = malloc(strlen(input) * 2 + 1);
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
    chunk.stream_buffer[0] = '\0';
    chunk.assistant_reply = malloc(1);
    chunk.assistant_reply[0] = '\0';
    chunk.reasoning_content = malloc(1);  // 初始化 reasoning_content
    chunk.reasoning_content[0] = '\0';

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (!curl) return NULL;

    char *json_str = malloc(4096);
    strcpy(json_str, "{\"model\": \"");
    strcat(json_str, deep_search_mode ? "deepseek-reasoner" : "deepseek-chat");
    strcat(json_str, "\", \"messages\": [");

    for (int i = 0; i < message_count; i++) {
        char *escaped = json_escape(messages[i].content);
        char msg[10240];
        snprintf(msg, sizeof(msg), "{\"role\":\"%s\",\"content\":\"%s\"}", messages[i].role, escaped);
        free(escaped);

        strcat(json_str, msg);
        if (i < message_count - 1) strcat(json_str, ",");
    }
    strcat(json_str, "], \"stream\": true}");

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    char auth[128];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", api_key);
    headers = curl_slist_append(headers, auth);

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.deepseek.com/chat/completions");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

    res = curl_easy_perform(curl);

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
    char input[10240];

    printf("欢迎使用DeepSeek终端版\n");
    printf("作者：黯夜空間\n有帮助就给项目点点star吧\n");
    printf("当前模式：%s（输入'模式切换'切换）\n", deep_search_mode ? "深度搜索" : "普通聊天");

    while (1) {
        printf("\n我：");
        fgets(input, sizeof(input), stdin);
        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "模式切换") == 0) {
            deep_search_mode = !deep_search_mode;
            printf("已切换到%s模式\n", deep_search_mode ? "深度搜索" : "普通聊天");
            continue;
        }
        if (strcmp(input, "退出") == 0) break;

        strcpy(messages[message_count].role, "user");
        strcpy(messages[message_count].content, input);
        message_count++;

        printf("\nDS：");
        fflush(stdout); // 确保提示先显示
        char *reply = get_api_response(messages, message_count);
        if (reply) {
            strcpy(messages[message_count].role, "assistant");
            strcpy(messages[message_count].content, reply);
            message_count++;
            free(reply);
        }
        printf("\n");
    }

    printf("对话结束，谢谢使用！\n");
    return 0;
}

