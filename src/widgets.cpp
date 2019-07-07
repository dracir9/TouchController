
#include "widgets.h"

/********************************************************************************
    Canvas 
********************************************************************************/
canvas::canvas(bool updt):
    widget(updt)
{
}

canvas::~canvas()
{
    delete child;
}

vector2<int16_t>  canvas::getSize(tftLCD *tft)
{
    return vector2<int16_t>();
}

void canvas::render(tftLCD *tft, int16_t x, int16_t y, int16_t w, int16_t h)
{
    if (w <= 0) w = tft->width()-x;
    if (h <= 0) h = tft->height()-y;
    if (child) child->render(tft, x, y, w, h);
}

void canvas::attachComponent(widget *chld)
{
    child=chld;
}

void canvas::clear()
{
    if(child)
    {
        delete child;
    }
    child = NULL;
}

/********************************************************************************
    Vertical Box 
********************************************************************************/
verticalBox::verticalBox(uint8_t elem, bool updt):
    widget(updt), elNum(elem), child(new widget*[elem])
{
    for (uint8_t i = 0; i < elNum; i++)
    {
        child[i] = NULL;
    }
}

verticalBox::~verticalBox()
{
    if (child)
    {
        for (uint8_t i = 0; i < elNum; i++)
        {
            if(child[i]) delete child[i];
            child[i] = NULL;
        }
        delete[] child;
        child = NULL;
    } 
}

vector2<int16_t> verticalBox::getSize(tftLCD *tft)
{
    vector2<int16_t> size0;
    bool fillX = false, fillY = false;
    for (uint8_t i = 0; i < elNum; i++)
    {
        if(!child[i]) continue;
        vector2<int16_t> size1 = child[i]->getSize(tft);
        if (size1.x == 0)
        {
            size0.x = 0;
            fillX = true;
        } else if (!fillX)
        {
            if (size1.x > 0)
            {
                if (size0.x < 0)
                {
                    size0.x = 0;
                    fillX = true;
                } else if (size1.x > size0.x)
                {
                    size0.x = size1.x;
                }
            }else if (size1.x < 0)
            {
                if (size0.x > 0)
                {
                    size0.x = 0;
                    fillX = true;
                } else if (size1.x < size0.x)
                {
                    size0.x = size1.x;
                }
            }
        }
        if (size1.y == 0)
        {
            size0.y = 0;
        }
        else if (!fillY)
        {
            if (size1.y > 0 && size0.y < 0)
            {
                size0.y = 0;
                fillY = true;
            }else if (size1.y < 0 && size0.y > 0)
            {
                size0.y = 0;
                fillY = true;
            } else
            {
                size0.y += size1.y;
            }
        }
    }
    return size0;
}

bool verticalBox::attachComponent(widget *chld)
{
    for (uint8_t i = 0; i < elNum; i++)
    {
        if (!child[i])
        {
            child[i] = chld;
            return true;
        }
    }
    return false;
}

void verticalBox::render(tftLCD *tft, int16_t x, int16_t y, int16_t w, int16_t h)
{
    if (!update && init) return;
    vector2<int16_t> size;
    uint16_t resHeight = 0;
    uint8_t fillNum = 0;
    for (uint8_t i = 0; i < elNum; i++)
    {
        if(child[i])
        {
            size = child[i]->getSize(tft);
            size.y == 0 ? fillNum++ : resHeight += abs(size.y) ;
        }
    }

    int16_t a,b,c,d;
    a=c=d=0;
    b=y;
    for (uint8_t i = 0; i < elNum; i++)
    {
        if(child[i])
        {
            size = child[i]->getSize(tft);
            if (size.x == 0)
            {
                a = x;
                c = w;
            } else if (size.x > 0)
            {
                a = x;
                c = size.x;
            } else
            {
                a = x + w + size.x;
                c = -size.x;
            }

            if (size.y == 0)
            {
                d = (h-resHeight)/fillNum;
            } else if (size.y > 0)
            {
                d = size.y;
            } else
            {
                d = -size.y;
                if (!fillNum)
                {
                    b = h - resHeight + b;
                    fillNum = 1;
                }
            }
            child[i]->render(tft, a, b, c, d);
            b += d;
        }
    }

    #ifdef DEBUG_LINES
        size = getSize(tft);
        if (size.x == 0)
        {
            size.x = w;
        } else if (size.x < 0)
        {
            x += w + size.x;
        }
        if (size.y == 0)
        {
            size.y = h;
        } else if (size.y < 0)
        {
            y += h + size.y;
        }
        tft->drawRect(x, y, abs(size.x), abs(size.y), TFT_GREEN);
    #endif

    init = true;
}

/********************************************************************************
    Horizontal Box 
********************************************************************************/



/********************************************************************************
    Text Box 
********************************************************************************/
vector2<int16_t>  textBox::getSize(tftLCD *tft)
{
    tft->setTextSize(size);
    tft->setFont(font);
    vector2<int16_t> size1 = tft->getTextBounds(*text);
    vector2<int16_t> size0;
    switch (arrange)
    {
    case fillMode::TopLeft:
        size0.x=size1.x+2*padding;
        size0.y=size1.y+2*padding;
        break;

    case fillMode::TopCenter:
        size0.x=0;
        size0.y=size1.y+2*padding;
        break;

    case fillMode::TopRight:
        size0.x=-size1.x-2*padding;
        size0.y=size1.y+2*padding;
        break;

    case fillMode::CenterLeft:
        size0.x=size1.x+2*padding;
        size0.y=0;
        break;
    
    case fillMode::CenterCenter:
        size0.x=0;
        size0.y=0;
        break;

    case fillMode::CenterRight:
        size0.x=-size1.x-2*padding;
        size0.y=0;
        break;

    case fillMode::BotLeft:
        size0.x=size1.x+2*padding;
        size0.y=-size1.y-2*padding;
        break;

    case fillMode::BotCenter:
        size0.x=0;
        size0.y=-size1.y-2*padding;
        break;

    case fillMode::BotRight:
        size0.x=-size1.x-2*padding;
        size0.y=-size1.y-2*padding;
        break;

    default:
        break;
    }
    return size0;
}

void textBox::render(tftLCD *tft, int16_t x, int16_t y, int16_t w, int16_t h)
{
    if (!update && init) return;
    tft->setTextSize(size);
    tft->setTextColor(txtcolor, bgcolor);
    tft->setFont(font);
    tft->setCursor(x+w/2, y+h/2);
    tft->printCenter(*text);
#ifdef DEBUG_LINES
    vector2<int16_t> size = tft->getTextBounds(*text);
    tft->drawRect(x, y, w, h, TFT_RED);
    tft->drawRect(x+(w-size.x)/2, y+(h-size.y)/2, size.x, size.y, TFT_BLUE);
#endif
    init = true;
}
