//
//  Tool.h
//  interface
//
//  Copyright (c) 2013 High Fidelity, Inc. All rights reserved.
//

#ifndef __interface__Tool__
#define __interface__Tool__

#include <QAction>

#include "InterfaceConfig.h"
#include "Util.h"

class QAction;


// Number of rows and columns in the SVG file for the tool palette
static const int   NUM_TOOLS_ROWS = 10;
static const int   NUM_TOOLS_COLS =  2;
static const int   SWATCHS_TOOLS_COUNT = 13; // 8 swatch + 5 tools

static const int   WIDTH_MIN = 47; // Minimal tools width
static const float TOOLS_RATIO = 40.0f / 60.0f; // ratio height/width of tools icons
static const float PAL_SCREEN_RATIO = 3.0f / 100.0f; // Percentage of the screeen width the palette is going to occupy

// Swatch popup consts
static const float POPUP_STEP         = 0.05f;
static const float POPUP_MARGIN       = 10.0f;
static const int   POPUP_WIDTH        = 280;
static const int   POPUP_HEIGHT       = 30;
static const int   SWATCH_WIDTH       = 64;
static const int   SWATCH_HEIGHT      = 30;
static const int   FIRST_LINE_OFFSET  = 12;
static const int   SECOND_LINE_OFFSET = 28;

class Tool {
public:
    Tool(QAction* action, GLuint texture, int x, int y);

    void setAction(QAction* action);
    bool isActive();
    virtual void render(int width, int height);

protected:
    QAction* _action;
    GLuint _texture;

    // position in the SVG grid
    double _x;
    double _y;
};

#endif /* defined(__interface__Tool__) */
