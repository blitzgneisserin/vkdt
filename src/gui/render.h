#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// initialise imgui. has to go after glfw and vulkan surface have been inited.
// will steal whatever is needed from global qvk struct.
int dt_gui_init_imgui();

// tear down imgui and free resources
void dt_gui_cleanup_imgui();

// poll imgui events from given event
// return non-zero if event should not be passed on
int dt_gui_poll_event_imgui(GLFWwindow *w, int key, int scancode, int action, int mods);

void dt_gui_record_command_buffer_imgui(VkCommandBuffer cmd_buf);

void dt_gui_set_lod(int lod);

#ifdef __cplusplus
}
#endif
