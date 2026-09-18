with open("build_meshes.txt") as f1, open("content/scene_runtime.cpp") as f2:
    t = f1.read()
    c = f2.read()
    c = c.replace("namespace tulpar::engine::content {", "namespace tulpar::engine::content {\n" + t)
with open("content/scene_runtime.cpp", "w") as f3:
    f3.write(c)
