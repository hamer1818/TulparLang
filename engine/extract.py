with open("third_party/imgui/imgui_demo.cpp") as f:
    lines = f.readlines()
with open("app/editor_browser.hpp", "w") as f:
    f.writelines(lines[11215:11750])
