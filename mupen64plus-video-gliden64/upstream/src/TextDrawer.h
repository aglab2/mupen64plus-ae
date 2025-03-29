#ifndef TEXTDRAWER_H
#define TEXTDRAWER_H

#include <memory>
#include "Graphics/ShaderProgram.h"
#include "Graphics/FramebufferTextureFormats.h"

struct Atlas;

class TextDrawer
{
public:
	void init();

	void destroy();

	void drawText(const char *_pText, float x, float y) const;

	void getTextSize(const char *_pText, float & _w, float & _h) const;

	void setTextColor(float * _color);

private:
};

extern TextDrawer g_textDrawer;

#endif // TEXTDRAWER_H
