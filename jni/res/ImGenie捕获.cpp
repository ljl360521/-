// ImGenie 捕获纹理回调模块实现

#define IMGUI_DEFINE_MATH_OPERATORS
#include "ImGenie捕获.h"
#include "imgui_internal.h"
#include "imgui_impl_opengl3.h"

#include <GLES3/gl3.h>
#include <android/log.h>

// ---------- 创建捕获纹理 ----------
ImTextureRef 捕获_创建纹理(int32_t width, int32_t height, ImDrawData* drawData) {
    if (width <= 0 || height <= 0 || !drawData) return ImTextureRef{};

    GLint prevFramebuffer = 0;
    GLint prevTexture = 0;
    GLfloat prevClearColor[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFramebuffer);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, prevClearColor);

    GLuint texId = 0;
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texId, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        __android_log_print(ANDROID_LOG_ERROR, "ImGenie", "FBO incomplete: 0x%x", status);
        glBindFramebuffer(GL_FRAMEBUFFER, prevFramebuffer);
        glBindTexture(GL_TEXTURE_2D, (GLuint)prevTexture);
        glClearColor(prevClearColor[0], prevClearColor[1], prevClearColor[2], prevClearColor[3]);
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &texId);
        return ImTextureRef{};
    }

    // 保存并扩展裁剪矩形
    ImVector<ImVec4> original_clips;
    int total_cmds = 0;
    for (int n = 0; n < drawData->CmdListsCount; n++)
        total_cmds += drawData->CmdLists[n]->CmdBuffer.Size;
    original_clips.reserve(total_cmds);

    ImVec2 display_size = ImGui::GetIO().DisplaySize;
    for (int n = 0; n < drawData->CmdListsCount; n++) {
        ImDrawList* cmd_list = drawData->CmdLists[n];
        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
            ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            original_clips.push_back(pcmd->ClipRect);

            if (pcmd->ClipRect.x <= 0.5f) pcmd->ClipRect.x = -65536.0f;
            if (pcmd->ClipRect.y <= 0.5f) pcmd->ClipRect.y = -65536.0f;
            if (pcmd->ClipRect.z >= display_size.x - 0.5f) pcmd->ClipRect.z = 65536.0f;
            if (pcmd->ClipRect.w >= display_size.y - 0.5f) pcmd->ClipRect.w = 65536.0f;
        }
    }

    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(drawData);

    // 恢复原始裁剪矩形
    int cmd_idx = 0;
    for (int n = 0; n < drawData->CmdListsCount; n++) {
        ImDrawList* cmd_list = drawData->CmdLists[n];
        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
            cmd_list->CmdBuffer[cmd_i].ClipRect = original_clips[cmd_idx++];
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, prevFramebuffer);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prevTexture);
    glClearColor(prevClearColor[0], prevClearColor[1], prevClearColor[2], prevClearColor[3]);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

    glDeleteFramebuffers(1, &fbo);

    ImTextureRef ref;
    ref._TexID = (ImTextureID)(intptr_t)texId;
    return ref;
}

// ---------- 销毁捕获纹理 ----------
void 捕获_销毁纹理(const ImTextureRef& tex) {
    GLuint texId = (GLuint)(intptr_t)tex._TexID;
    if (texId != 0) {
        glDeleteTextures(1, &texId);
    }
}