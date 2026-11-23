// The smallest useful SimplyUI program.
// Build: g++ main.cpp -I../../include -o hello.exe -mwindows   (run it with simplyui.dll next to it)
#include "simplyui.h"

int main() {
    std::string name = "World";
    float volume = 0.5f;
    bool loud = false;

    return sui::Run("Hello SimplyUI", 420, 260, [&] {
        sui::Heading("Hello, " + name + "!");
        sui::InputText("Name", name, "Type your name");
        sui::Slider("Volume", volume, 0.0f, 1.0f);
        sui::Toggle("Extra loud", loud);
        if (sui::PrimaryButton("Say hello"))
            sui::Toast(loud ? "HELLO, " + name + "!!!" : "Hello, " + name + "!", sui::Level::Success);
    });
}
