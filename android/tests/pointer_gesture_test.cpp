#include "../app/jni/android_pointer_gesture.h"
#include <cassert>
#include <iostream>
int main() {
    using G = AndroidPointerGesture;
    G click;
    auto r = click.Update(true, true, true, true, 100, 100, 0, 0, .016f, 0);
    assert(!r.toggle && !r.down && !r.hold); // Press must not change mode.
    r = click.Update(true, false, true, true, 100, 100, 0, 0, .016f, 100);
    assert(r.toggle && !r.up);
    r = click.Update(true, false, true, true, 100, 100, 0, 0, .016f, 116);
    assert(!r.toggle); // Toggle once per click.

    G drag;
    drag.Update(true, true, true, true, 100, 100, 0, 0, .016f, 0);
    r = drag.Update(true, true, true, true, 100, 100, 1, 0, .016f, 16);
    assert(r.down && r.hold && !r.toggle && r.x == 100);
    for (unsigned t = 32; t < 3200; t += 16)
        r = drag.Update(true, true, true, true, 100, 100, 1, 0, .016f, t);
    assert(r.hold && !r.down && r.x > 640); // Continuous pan past screen edge.
    r = drag.Update(true, false, true, true, 100, 100, 0, 0, .016f, 3200);
    assert(r.up && !r.toggle && r.x > 640);

    G left;
    left.Update(true, true, true, true, 100, 100, 0, 0, .016f, 0);
    r = left.Update(true, true, false, true, 120, 100, 0, 0, .016f, 16);
    assert(r.down && r.hold); // Map captures even if pointer leaves it.
    r = left.Update(true, true, false, true, 125, 100, 0, 0, .016f, 32);
    assert(r.x == 125);
    r = left.Update(true, false, false, true, 125, 100, 0, 0, .016f, 48);
    assert(r.up && !r.toggle);

    G outside;
    outside.Update(true, true, false, true, 100, 100, 1, 0, .016f, 0);
    r = outside.Update(true, true, true, true, 120, 100, 1, 0, .016f, 16);
    assert(!r.down && !r.hold); // Cannot start pan outside map then move over it.
    r = outside.Update(true, false, true, true, 120, 100, 0, 0, .016f, 32);
    assert(!r.toggle && !r.up);

    G longHold;
    longHold.Update(true, true, false, true, 100, 100, 0, 0, .016f, 0);
    r = longHold.Update(true, false, false, true, 100, 100, 0, 0, .016f, 500);
    assert(!r.toggle); // A long hold is not a short click.

    G cancel;
    cancel.Update(true, true, true, true, 100, 100, 0, 0, .016f, 0);
    cancel.Update(true, true, true, true, 100, 100, 1, 0, .016f, 16);
    r = cancel.Update(false, true, true, true, 100, 100, 1, 0, .016f, 32);
    assert(r.up && !r.toggle);
    r = cancel.Update(true, true, true, true, 100, 100, 1, 0, .016f, 48);
    assert(!r.down && !r.hold); // Disconnect/menu cancellation requires release.
    r = cancel.Update(true, false, true, true, 100, 100, 0, 0, .016f, 64);
    assert(!r.toggle);

    G close;
    close.Update(true, true, true, true, 100, 100, 0, 0, .016f, 0);
    close.Update(true, true, true, true, 100, 100, 1, 0, .016f, 16);
    r = close.Update(true, true, false, false, 100, 100, 1, 0, .016f, 32);
    assert(r.up && !r.toggle);
    std::cout << "R1 gesture tests passed\n";
}
