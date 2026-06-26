%module imgui
%{
#include "imgui.h"
%}

// 告诉SWIG忽略一些它不能处理的东西
%include <std_string.i>
%ignore ImGui::ImVector;

// 只定义你需要的接口，而不是整个头文件
namespace ImGui {
    bool Begin(const char* name, bool* p_open = NULL);
    void End();
    void Text(const char* fmt, ...);
    bool Button(const char* label);
    bool SliderFloat(const char* label, float* v, float v_min, float v_max);
    void SetNextWindowPos(float x, float y);
    void SetNextWindowSize(float w, float h);
}

// 或者使用部分include + ignore
// %include "imgui.h"
// %ignore ImGui::ImVector;
// %ignore ImGui::ImGuiStyle::operator=;