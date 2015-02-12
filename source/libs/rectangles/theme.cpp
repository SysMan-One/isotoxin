#include "rectangles.h"

using namespace ts;

const theme_rect_s *cached_theme_rect_c::operator()( ts::uint32 st ) const
{
	if (theme != &gui->theme())
	{
		theme = &gui->theme();
		rects[0] = theme->get_rect(themerect);
        rects[ RST_HIGHLIGHT ] = theme->get_rect(tmp_str_c(themerect).append(CONSTASTR(".h")));
        rects[ RST_ACTIVE ] = theme->get_rect(tmp_str_c(themerect).append(CONSTASTR(".a")));
        rects[ RST_FOCUS ] = theme->get_rect(tmp_str_c(themerect).append(CONSTASTR(".f")));

        rects[ RST_ACTIVE | RST_HIGHLIGHT ] = theme->get_rect(tmp_str_c(themerect).append(CONSTASTR(".ah")));
        if (!rects[ RST_ACTIVE | RST_HIGHLIGHT ]) rects[ RST_ACTIVE | RST_HIGHLIGHT ] = rects[ RST_HIGHLIGHT ];

        rects[ RST_FOCUS | RST_HIGHLIGHT ] = theme->get_rect(tmp_str_c(themerect).append(CONSTASTR(".fh")));
        if (!rects[ RST_FOCUS | RST_HIGHLIGHT ]) rects[ RST_FOCUS | RST_HIGHLIGHT ] = rects[ RST_HIGHLIGHT ];

        rects[ RST_FOCUS | RST_ACTIVE ] = theme->get_rect(tmp_str_c(themerect).append(CONSTASTR(".af")));
        if (!rects[ RST_FOCUS | RST_ACTIVE ]) rects[ RST_FOCUS | RST_ACTIVE ] = rects[ RST_ACTIVE ];

        rects[ RST_FOCUS | RST_ACTIVE | RST_HIGHLIGHT ] = theme->get_rect(tmp_str_c(themerect).append(CONSTASTR(".afh")));
        if (!rects[RST_FOCUS | RST_ACTIVE | RST_HIGHLIGHT]) rects[RST_FOCUS | RST_ACTIVE | RST_HIGHLIGHT] = rects[RST_ACTIVE | RST_HIGHLIGHT];

        for(int i=0;i<RST_ALL_COMBINATIONS;++i)
            if (!rects[i]) rects[i] = rects[0];
	}

	return rects[ASSERT(st < RST_ALL_COMBINATIONS) ? st : 0].get();
}

theme_rect_s::~theme_rect_s()
{
}

void theme_rect_s::init_subimage(subimage_e si, const str_c &sidef)
{
	siso[si].fillcolor = 0;

    str_c tail;
	sis[si] = parserect(sidef, ts::irect(0), &tail);
    siso[si].tile = !tail.equals(CONSTASTR("stretch"));
    if (siso[si].tile && tail.begins(CONSTASTR("#")))
        siso[si].fillcolor = ts::parsecolor<char>(tail, ARGB(0,0,0,255));
    siso[si].loaded = !sidef.is_empty();
}

void theme_rect_s::load_params(const abp_c * block)
{
    static const char *sins[SI_count] = { "lt", "rt", "lb", "rb", "l", "t", "r", "b", "c", "base",
        "capstart", "caprep", "capend",
        "sbtop", "sbrep", "sbbot", "smtop", "smrep", "smbot" };

    const abp_c * byrect = block->get( CONSTASTR("byrect") );

    for (int i = 0; i < SI_count; ++i)
        init_subimage((subimage_e)i, block->get_string(sins[i]));

	resizearea = block->get_int("resizearea", 2);
	hollowborder = parserect( block->get_string(CONSTASTR("hollowborder")), ts::irect(0) );
    maxcutborder = parserect( block->get_string(CONSTASTR("maxcutborder")), ts::irect(0) );
	clientborder = parserect( block->get_string(CONSTASTR("clientborder")), ts::irect(0) );
	minsize = parsevec2( block->get_string(CONSTASTR("minsize")), ts::ivec2(0) );
    capbuttonsshift = parsevec2( block->get_string(CONSTASTR("bshift")), ts::ivec2(0) );
    capbuttonsshift_max = parsevec2( block->get_string(CONSTASTR("bshiftmax")), ts::ivec2(0) );

    captexttab = block->get_int(CONSTASTR("captexttab"), 5);
	captop = block->get_int(CONSTASTR("captop"), 0);
    captop_max = block->get_int(CONSTASTR("captop_max"), 0);
	capheight = block->get_int(CONSTASTR("capheight"), 0);
    capheight_max = block->get_int(CONSTASTR("capheight_max"), 0);

    ts::str_c d(gui->default_font());
    capfont = block->get_string(CONSTASTR("capfont"), d);
    deffont = block->get_string(CONSTASTR("deffont"), d);

    deftextcolor = parsecolor( block->get_string(CONSTASTR("deftextcolor")).as_sptr(), ARGB(0,0,0) );
    ts::token<char> cols( block->get_string(CONSTASTR("colors")), ',' );
    colors.clear();
    for(;cols;++cols)
        colors.add() = parsecolor<char>(*cols, deftextcolor);

    if (byrect)
    {
        ts::irect byrectr = parserect( byrect->as_string(), ts::irect(0) );

        if (!siso[SI_LEFT_TOP].loaded) sis[SI_LEFT_TOP] = ts::irect( byrectr.lt, byrectr.lt + clientborder.lt );
        if (!siso[SI_RIGHT_TOP].loaded) sis[SI_RIGHT_TOP] = ts::irect( byrectr.rb.x - clientborder.rb.x, byrectr.lt.y, byrectr.rb.x, byrectr.lt.y + clientborder.lt.y );
        if (!siso[SI_LEFT_BOTTOM].loaded) sis[SI_LEFT_BOTTOM] = ts::irect( byrectr.lt.x, byrectr.rb.y - clientborder.rb.y, byrectr.lt.x + clientborder.lt.x, byrectr.rb.y );
        if (!siso[SI_RIGHT_BOTTOM].loaded) sis[SI_RIGHT_BOTTOM] = ts::irect( byrectr.rb - clientborder.rb, byrectr.rb );
        if (!siso[SI_LEFT].loaded)
        {
            sis[SI_LEFT] = ts::irect( byrectr.lt.x, byrectr.lt.y + clientborder.lt.y, byrectr.lt.x + clientborder.lt.x, byrectr.rb.y - clientborder.rb.y );
            siso[SI_LEFT].tile = true;
            siso[SI_LEFT].fillcolor = 0;
        }
        if (!siso[SI_TOP].loaded)
        {
            sis[SI_TOP] = ts::irect( byrectr.lt.x + clientborder.lt.x, byrectr.lt.y, byrectr.rb.x - clientborder.rb.x, byrectr.lt.y + clientborder.lt.y );
            siso[SI_TOP].tile = true;
            siso[SI_TOP].fillcolor = 0;
        }
        if (!siso[SI_RIGHT].loaded)
        {
            sis[SI_RIGHT] = ts::irect( byrectr.rb.x - clientborder.rb.x, byrectr.lt.y + clientborder.lt.y, byrectr.rb.x, byrectr.rb.y - clientborder.rb.y );
            siso[SI_RIGHT].tile = true;
            siso[SI_RIGHT].fillcolor = 0;
        }
        if (!siso[SI_BOTTOM].loaded)
        {
            sis[SI_BOTTOM] = ts::irect( byrectr.lt.x + clientborder.lt.x, byrectr.rb.y - clientborder.rb.y, byrectr.rb.x - clientborder.rb.x, byrectr.rb.y );
            siso[SI_BOTTOM].tile = true;
            siso[SI_BOTTOM].fillcolor = 0;
        }
        if (!siso[SI_CENTER].loaded)
        {
            sis[SI_CENTER] = ts::irect(byrectr.lt + clientborder.lt, byrectr.rb - clientborder.rb);
            siso[SI_CENTER].tile = true;
            siso[SI_CENTER].fillcolor = 0;
        }

        if (!siso[SI_BASE].loaded)
        {
            sis[SI_BASE] = ts::irect( byrectr.lt + clientborder.lt, byrectr.rb - clientborder.rb );
            siso[SI_BASE].tile = true;
            siso[SI_BASE].fillcolor = 0;
        }
    }
}

button_desc_s::~button_desc_s()
{
}
void button_desc_s::load_params(theme_c *th, const abp_c * block)
{
    ts::str_c bx = block->get_string(CONSTASTR("build3"));
    int bxn = 3;
    if (bx.is_empty())
    {
        bx = block->get_string(CONSTASTR("build4"));
        bxn = 4;
    }
    if (!bx.is_empty())
    {
        // automatic build
        ts::token<char> build3( bx, ',' );
        ts::ivec2 p, sz, d;
        p.x = build3->as_int(); ++build3;
        p.y = build3->as_int(); ++build3;
        sz.x = build3->as_int(); ++build3;
        sz.y = build3->as_int(); ++build3;
        d.x = build3->as_int(); ++build3;
        d.y = build3->as_int();

        for (int i = 0; i < bxn; ++i)
        {
            rects[i].lt = p;
            rects[i].rb = p + sz;
            p += d;
        }
        if (bxn == 3)
            rects[DISABLED] = rects[NORMAL];

    } else
    {
        const ts::asptr stnames[numstates] = { CONSTASTR("normal"), CONSTASTR("hover"), CONSTASTR("press"), CONSTASTR("disabled") };
        for (int i = 0; i < numstates; ++i)
        {
            ts::str_c x = block->get_string(stnames[i]);
            rectsf[i] = th->get_rect(x);
            if (!rectsf[i])
                rects[i] = parserect(x, ts::irect(0));
            colors[i] = parsecolor<char>(block->get_string(sstr_t<32>(stnames[i], CONSTASTR("textcolor"))), ARGB(0, 0, 0));
        }
    }

    text = block->get_string(CONSTASTR("text"));

    ts::token<char> salign( block->get_string(CONSTASTR("align")), ',' );
    for( ;salign; ++salign )
        if ( salign->equals(CONSTASTR("left") ) ) align |= ALEFT;
        else if ( salign->equals(CONSTASTR("top") ) ) align |= ATOP;
        else if ( salign->equals(CONSTASTR("right") ) ) align |= ARIGHT;
        else if ( salign->equals(CONSTASTR("bottom") ) ) align |= ABOTTOM;

    size = ts::ivec2(10);
    for (int i = 0; i < numstates; ++i)
    {
        if (rects[i].size().x > size.x)
            size.x = rects[i].size().x;
        if (rects[i].size().y > size.y)
            size.y = rects[i].size().y;
    }
}

ts::ivec2 button_desc_s::draw( rectengine_c *engine, states st, const ts::irect& area, ts::uint32 defalign )
{
    ts::uint32 a = align;
    if (0 == (a & (ALEFT | ARIGHT))) a |= defalign & (ALEFT | ARIGHT);
    if (0 == (a & (ATOP | ABOTTOM))) a |= defalign & (ATOP | ABOTTOM);

    ts::ivec2 sz = rects[st].size();
    ts::ivec2 p = area.lt;
    switch ( a & (ALEFT | ARIGHT) )
    {
        case ARIGHT:
            p.x = area.rb.x - sz.x;
            break;
        case ALEFT|ARIGHT:
            p.x += (area.width() - sz.x) / 2;
            break;
    }
    switch (a & (ATOP | ABOTTOM))
    {
        case ABOTTOM:
            p.y = area.rb.y - sz.y;
            break;
        case ATOP | ABOTTOM:
            p.y += (area.height() - sz.y) / 2;
            break;
    }
    engine->draw(p,src,rects[st],is_alphablend(st));
    return p;
}


theme_c::theme_c()
{
}

theme_c::~theme_c()
{

}

const drawable_bitmap_c &theme_c::loadimage( const wsptr &path, const wsptr &name )
{
	drawable_bitmap_c &dbmp = bitmaps[name];
    if (dbmp.info().pitch != 0)
        return dbmp;
    bitmap_c bmp; bmp.load_from_file( fn_join(pwstr_c(path), name) );
    dbmp.create_from_bitmap(bmp, false, true);
	return dbmp;
}

bool theme_c::load( const ts::wsptr &name )
{
    clear_glyphs_cache();

	wstr_c path(CONSTWSTR("themes/")); path.append(name).append_char('/');
	int pl = path.get_length();
	abp_c bp;
	if (!g_fileop->load(path.append(CONSTWSTR("struct.decl")), bp)) return false;
	path.set_length(pl);

    set_fonts_dir(path);
    set_images_dir(path);

    bitmaps.clear();
    rects.clear();
    buttons.clear();

    theme_conf_s thc;
    if (const abp_c * conf = bp.get("conf"))
    {
        thc.fastborder = conf->get_int(CONSTASTR("fastborder"), 0) != 0;
        thc.rootalphablend = conf->get_int(CONSTASTR("rootalphablend"), 1) != 0;
        thc.specialborder = conf->get_int(CONSTASTR("specialborder"), 0) != 0;
    }


	if (const abp_c * rs = bp.get("rects"))
	{
		for (auto it = rs->begin(); it; ++it)
		{
			shared_ptr<theme_rect_s> &r = rects[ it.name() ];
            str_c bn = it->as_string();
            while (!bn.is_empty())
            {
                const abp_c *parent = rs->get(bn);
                bn.clear();
                if (parent)
                {
                    it->merge(*parent, abp_c::SKIP_EXIST);
                    bn = parent->as_string();
                }
            }
            const drawable_bitmap_c &dbmp = loadimage(path,to_wstr(it->get_string("src")));
			r = theme_rect_s::build( dbmp, thc );
			
			r->load_params(it);
		}
	}

    if (const abp_c * imgs = bp.get("images"))
    {
        for (auto it = imgs->begin(); it; ++it)
        {
            str_c src = it->get_string(CONSTASTR("src"));
            if (ASSERT(!src.is_empty()))
            {
                token<char> t(src.as_sptr());
                const drawable_bitmap_c &dbmp = loadimage(path, to_wstr(t->as_sptr()));
                ++t;
                irect r = ts::parserect(t,irect(ivec2(0),dbmp.info().sz));
                add_image(to_wstr(it.name()),dbmp.body(r.lt),imgdesc_s(r.size(),32,dbmp.info().pitch),false /* no need to copy */);
            }
        }
    }

    if (const abp_c * btns = bp.get("buttons"))
    {
        for (auto it = btns->begin(); it; ++it)
        {
            shared_ptr<button_desc_s> &bd = buttons[it.name()];
            ts::str_c src = it->get_string(CONSTASTR("src"));
            if (src.is_empty())
            {
                bd = button_desc_s::build( make_dummy<drawable_bitmap_c>(true) );
            } else
            {
                const drawable_bitmap_c &dbmp = loadimage(path, to_wstr(src));
                bd = button_desc_s::build(dbmp);
            }
            bd->load_params(this, it);
        }
    }

    if (const abp_c * fonts = bp.get("fonts"))
    {
        for (auto it = fonts->begin(); it; ++it)
        {
            add_font(it.name(), it->as_string() );
        }
    }
    
    ts::g_default_text_font.assign(CONSTASTR("default"));

    if (const abp_c * corrs = bp.get("corrections"))
    {
        for (auto it = corrs->begin(); it; ++it)
        {
            const str_c &src = it->as_string();
            if (ASSERT(!src.is_empty()))
            {
                token<char> t(src.as_sptr());
                drawable_bitmap_c &dbmp = const_cast<drawable_bitmap_c &>(loadimage(path, to_wstr(t->as_sptr())));
                ++t;
                irect r = ts::parserect(t, irect(ivec2(0), dbmp.info().sz));
                if (it.name().equals( CONSTASTR("zeroalpha") ))
                {
                    dbmp.fill_alpha(r.lt, r.size(), 1);
                } else if (it.name().equals(CONSTASTR("highlight")))
                {
                    struct
                    {
                        ts::TSCOLOR c;

                        void point(uint8 * me, const image_extbody_c::FMATRIX &m)
                        {
                            // dst = src + (1 - src.a) * dst;
                            // need minimal src_a 
                            // but final dst should not exceed 255

                            // 1 >= src + (1 - src.a) * dst;
                            // 1 - src >= dst - src.a * dst;
                            // -(1 - src - dst)/dst >= src.a;
                            // 1 - 1/dst + src/dst >= src.a

                            const uint8 * src = m[1][1];

                            float a0 = me[0] == 0 ? 0.0f : (1.0f + ((float)src[0] - 255.0f) / me[0]);
                            float a1 = me[1] == 0 ? 0.0f : (1.0f + ((float)src[1] - 255.0f) / me[1]);
                            float a2 = me[2] == 0 ? 0.0f : (1.0f + ((float)src[2] - 255.0f) / me[2]);

                            int a = ts::lround( 255.0f * ts::tmax( a0, a1, a2 ) );
                            if (a < 1) a = 1;

                            if (me[3])
                                me[3] = as_byte(a);

                        }
                    } f;
                    f.c = ts::parsecolor<char>(*t, ARGB(0, 0, 0, 255));

                    dbmp.apply_filter(r.lt, r.size(),f);
                }
            }
        }
    }


	m_name = name;
	return true;
}

irect theme_rect_s::captionrect( const ts::irect &rr, bool maximized ) const
{
	irect r;
    if (maximized || fastborder())
    {
        r.lt = clientborder.lt - maxcutborder.lt + rr.lt;
        r.rb.x = rr.width() - clientborder.rb.x + maxcutborder.rb.x + rr.lt.x;
        r.rb.y = r.lt.y + capheight_max;
    } else
    {
        r.lt = clientborder.lt + rr.lt;
        r.rb.x = rr.width() - clientborder.rb.x + rr.lt.x;
        r.rb.y = r.lt.y + capheight;
    }
	return r;

}

irect theme_rect_s::clientrect( const ts::ivec2 &sz, bool maximized ) const		// calc raw client area
{
	irect r;
    if (maximized || fastborder())
    {
        r.lt = clientborder.lt - maxcutborder.lt;
        r.rb = sz - clientborder.rb + maxcutborder.rb;
        r.lt.y += capheight_max;
    } else
    {
        r.lt = clientborder.lt;
        r.rb = sz - clientborder.rb;
        r.lt.y += capheight;
    }
	return r;
}

irect theme_rect_s::hollowrect( const rectprops_c &rps ) const	// calc client area and resize area
{
	irect r;
    if (rps.is_maximized() || fastborder())
    {
        r.lt = ts::ivec2(0);
        r.rb = rps.currentsize();
    } else
    {
        r.lt = hollowborder.lt;
        r.rb = rps.currentsize() - hollowborder.rb;
    }
	return r;
}
