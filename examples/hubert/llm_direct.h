/*
 * llm_direct.h — direct Ollama chat client.
 *
 * Opens a TCP connection to the LLM host, POSTs an ``/api/chat`` request,
 * and streams the response back through a caller-supplied callback. No
 * bridge involvement — the terminal talks to spark.hitorro.com by itself.
 *
 * Conversation state is not persisted here; the caller owns the message
 * list so it can decide what to keep across turns.
 */
#ifndef HUBERT_LLM_DIRECT_H
#define HUBERT_LLM_DIRECT_H

typedef struct {
    const char *host;
    int         port;
    const char *model;
    int         connect_timeout_ms;
    int         io_timeout_ms;
} LlmConfig;

/* Roles echoed to the model. Convention matches Ollama's /api/chat. */
typedef enum {
    LLM_ROLE_SYSTEM = 0,
    LLM_ROLE_USER,
    LLM_ROLE_ASSISTANT,
    LLM_ROLE_TOOL,
} LlmRole;

/* Fixed-size message buffer so we can keep the whole thing malloc-free. */
#define LLM_MAX_MESSAGES     32
#define LLM_MAX_MESSAGE_LEN  1024

typedef struct {
    LlmRole role;
    /* For tool messages, this is the tool result text; the model treats it
     * as {role:"tool", content:...}. */
    char    content[LLM_MAX_MESSAGE_LEN];
} LlmMessage;

typedef struct {
    LlmMessage entries[LLM_MAX_MESSAGES];
    int        count;
} LlmMessages;

void llm_msgs_init(LlmMessages *m);
int  llm_msgs_add(LlmMessages *m, LlmRole role, const char *content);

/* Callback fired for each decoded frame. `content` is the new token delta
 * (empty on control frames). If `tool_name` is non-empty the model asked
 * to run a tool — the caller executes it and appends a LLM_ROLE_TOOL
 * message to `msgs` before the next turn. `done` is set on the terminal
 * frame; the caller decides whether to loop. */
typedef struct {
    const char *content;
    const char *tool_name;
    const char *tool_args;     /* raw JSON of the arguments object */
    int         done;
} LlmDelta;

typedef void (*LlmDeltaFn)(const LlmDelta *delta, void *userdata);

/* Run one turn. Sends the entire message list; streams tokens back via cb.
 * Returns 0 on success, non-zero on transport / parse failure.
 * out_tool_name / out_tool_args (if non-NULL) receive the last tool call
 * seen; empty if none. */
int llm_chat_stream(const LlmConfig *cfg,
                    const LlmMessages *msgs,
                    LlmDeltaFn cb, void *userdata,
                    char *out_tool_name, int tn_size,
                    char *out_tool_args, int ta_size);

#endif
