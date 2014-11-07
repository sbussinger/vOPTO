#ifndef VDOS_MOUSE_H
#define VDOS_MOUSE_H

//void Mouse_CursorMoved(Bit16s xrel, Bit16s yrel, float x, float y);
void Mouse_CursorMoved(Bit16u x, Bit16u y, Bit16u scale_x, Bit16u scale_y);
void Mouse_ButtonPressed(Bit8u button);
void Mouse_ButtonReleased(Bit8u button);

extern Bit8u mouse_event_type;
#endif
