// license:BSD-3-Clause
// copyright-holders:Aaron Giles
/*********************************************************************

    dvstate.c

    State debugger view.

***************************************************************************/

#include "emu.h"
#include "dvstate.h"

#include "debugvw.h"

#include "screen.h"


const int debug_view_state::REG_DIVIDER;
const int debug_view_state::REG_CYCLES;
const int debug_view_state::REG_BEAMX;
const int debug_view_state::REG_BEAMY;
const int debug_view_state::REG_FRAME;


//**************************************************************************
//  DEBUG VIEW STATE SOURCE
//**************************************************************************

//-------------------------------------------------
//  debug_view_state_source - constructor
//-------------------------------------------------

debug_view_state_source::debug_view_state_source(const char *name, device_t &device)
	: debug_view_source(name, &device)
	, m_stateintf(dynamic_cast<device_state_interface *>(&device))
	, m_execintf(dynamic_cast<device_execute_interface *>(&device))
{
}



//**************************************************************************
//  DEBUG VIEW STATE
//**************************************************************************

//-------------------------------------------------
//  debug_view_state - constructor
//-------------------------------------------------

debug_view_state::debug_view_state(running_machine &machine, debug_view_osd_update_func osdupdate, void *osdprivate)
	: debug_view(machine, DVT_STATE, osdupdate, osdprivate)
	, m_divider(0)
	, m_last_update(0)
{
	// fail if no available sources
	enumerate_sources();
	if (m_source_list.count() == 0)
		throw std::bad_alloc();

	// configure the view
	m_supports_cursor = true;
}


//-------------------------------------------------
//  ~debug_view_state - destructor
//-------------------------------------------------

debug_view_state::~debug_view_state()
{
	reset();
}


//-------------------------------------------------
//  enumerate_sources - enumerate all possible
//  sources for a registers view
//-------------------------------------------------

void debug_view_state::enumerate_sources()
{
	// start with an empty list
	m_source_list.reset();

	// iterate over devices that have state interfaces
	std::string name;
	for (device_state_interface &state : state_interface_iterator(machine().root_device()))
	{
		name = string_format("%s '%s'", state.device().name(), state.device().tag());
		m_source_list.append(*global_alloc(debug_view_state_source(name.c_str(), state.device())));
	}

	// reset the source to a known good entry
	set_source(*m_source_list.first());
}


//-------------------------------------------------
//  reset - delete all of our state items
//-------------------------------------------------

void debug_view_state::reset()
{
	// free all items in the state list
	m_state_list.clear();
}


//-------------------------------------------------
//  recompute - recompute all info for the
//  registers view
//-------------------------------------------------

void debug_view_state::recompute()
{
	const debug_view_state_source &source = downcast<const debug_view_state_source &>(*m_source);

	// start with a blank list
	reset();

	// add a cycles entry: cycles:99999999
	m_state_list.emplace_back(REG_CYCLES, "cycles", 8);

	screen_device_iterator screen_iterator = screen_device_iterator(machine().root_device());
	screen_device_iterator::auto_iterator iter = screen_iterator.begin();
	const int screen_count = screen_iterator.count();

	if (screen_count == 1)
	{
		// add a beam entry: beamx:1234
		m_state_list.emplace_back(REG_BEAMX, "beamx", 4);

		// add a beam entry: beamy:5678
		m_state_list.emplace_back(REG_BEAMY, "beamy", 4);

		// add a beam entry: frame:123456
		m_state_list.emplace_back(REG_FRAME, "frame", 6);
	}
	else if (screen_count > 1)
	{
		for (int i = 0; (i < screen_count) && (i < 8); i++, iter++)
		{
			m_state_list.emplace_back(REG_BEAMX_S0 - i, string_format("beamx%d", i).c_str(), 4);
			m_state_list.emplace_back(REG_BEAMY_S0 - i, string_format("beamy%d", i).c_str(), 4);
			m_state_list.emplace_back(REG_FRAME_S0 - i, string_format("frame%d", i).c_str(), 6);
		}
	}

	// add a flags entry: flags:xxxxxxxx
	m_state_list.emplace_back(STATE_GENFLAGS, "flags", source.m_stateintf->state_string_max_length(STATE_GENFLAGS));

	// add a divider entry
	m_state_list.emplace_back(REG_DIVIDER, "", 0);

	m_registers_start = m_state_list.size();

	// add all registers into it
	for (auto &entry : source.m_stateintf->state_entries())
	{
		if (entry->divider())
			m_state_list.emplace_back(REG_DIVIDER, "", 0);
		else if (entry->visible())
			m_state_list.emplace_back(entry->index(), entry->symbol(), source.m_stateintf->state_string_max_length(entry->index()));
	}

	// count the entries and determine the maximum tag and value sizes
	std::size_t count = 0;
	std::size_t maxtaglen = 0;
	u8 maxvallen = 0;
	for (auto const &item : m_state_list)
	{
		count++;
		maxtaglen = (std::max)(maxtaglen, item.m_symbol.length());
		maxvallen = (std::max)(maxvallen, item.value_length());
	}

	// set the current divider and total cols
	m_divider = unsigned(1U + maxtaglen + 1U);
	m_total.x = u32(1U + maxtaglen + 2U + maxvallen + 1U);
	m_total.y = u32(count);
	m_topleft.x = 0;
	m_topleft.y = 0;

	// setting up cursor position
	m_cursor.x = m_divider + 1;
	m_cursor.y = m_registers_start;

	// no longer need to recompute
	m_recompute = false;
}


//-------------------------------------------------
//  view_notify - handle notification of updates
//  to cursor changes
//-------------------------------------------------

void debug_view_state::view_notify(debug_view_notification type)
{
	if (type == VIEW_NOTIFY_CURSOR_CHANGED)
	{
		if (m_cursor.y < m_registers_start)
			m_cursor.y = m_registers_start;
		else if (m_cursor.y >= m_state_list.size())
				m_cursor.y = m_state_list.size() - 1;

		if (m_cursor.x < m_divider + 1)
			m_cursor.x = m_divider + 1;
		else
		{
			state_item &curitem(m_state_list[m_cursor.y]);

			if (m_cursor.x > m_divider + curitem.value_length())
				m_cursor.x = m_divider + curitem.value_length();
		}
	}
	else if (type == VIEW_NOTIFY_SOURCE_CHANGED)
		m_recompute = true;
}


//-------------------------------------------------
//  view_update - update the contents of the
//  register view
//-------------------------------------------------

void debug_view_state::view_update()
{
	// if our assumptions changed, revisit them
	if (m_recompute)
		recompute();

	// get cycle count if we have an execute interface
	debug_view_state_source const &source(downcast<debug_view_state_source const &>(*m_source));
	u64 const total_cycles(source.m_execintf ? source.m_execintf->total_cycles() : 0);
	bool const cycles_changed(m_last_update != total_cycles);

	// loop over rows
	auto it(m_state_list.begin());
	debug_view_char *dest(&m_viewdata[0]);
	for (s32 index = 0, limit = m_topleft.y + m_visible.y; (index < limit) || (it != m_state_list.end()); ++index)
	{
		bool const visible((index >= m_topleft.y) && (index < limit));
		u32 col(0);

		if (it != m_state_list.end())
		{
			// advance to the next item
			state_item &curitem(*it++);

			// update item and get the effective string
			std::string valstr;
			switch (curitem.index())
			{
			case REG_DIVIDER:
				curitem.m_symbol.clear();
				curitem.m_symbol.resize(m_total.x, '-');
				break;

			case REG_CYCLES:
				curitem.update(source.m_execintf ? source.m_execintf->cycles_remaining() : 0, cycles_changed);
				valstr = string_format("%-8d", curitem.value());
				break;

			case REG_BEAMX_S0: case REG_BEAMX_S1: case REG_BEAMX_S2: case REG_BEAMX_S3:
			case REG_BEAMX_S4: case REG_BEAMX_S5: case REG_BEAMX_S6: case REG_BEAMX_S7:
				curitem.update(screen_device_iterator(machine().root_device()).byindex(-(curitem.index() - REG_BEAMX_S0))->hpos(), cycles_changed);
				valstr = string_format("%4d", curitem.value());
				break;

			case REG_BEAMY_S0: case REG_BEAMY_S1: case REG_BEAMY_S2: case REG_BEAMY_S3:
			case REG_BEAMY_S4: case REG_BEAMY_S5: case REG_BEAMY_S6: case REG_BEAMY_S7:
				curitem.update(screen_device_iterator(machine().root_device()).byindex(-(curitem.index() - REG_BEAMY_S0))->vpos(), cycles_changed);
				valstr = string_format("%4d", curitem.value());
				break;

			case REG_FRAME_S0: case REG_FRAME_S1: case REG_FRAME_S2: case REG_FRAME_S3:
			case REG_FRAME_S4: case REG_FRAME_S5: case REG_FRAME_S6: case REG_FRAME_S7:
				curitem.update(screen_device_iterator(machine().root_device()).byindex(-(curitem.index() - REG_FRAME_S0))->frame_number(), cycles_changed);
				valstr = string_format("%-6d", curitem.value());
				break;

			default:
				curitem.update(source.m_stateintf->state_int(curitem.index()), cycles_changed);
				valstr = source.m_stateintf->state_string(curitem.index());
			}

			// if this row is visible, add it to the buffer
			if (visible)
			{
				// see if we changed
				const u8 attrib(curitem.changed() ? DCA_CHANGED: DCA_NORMAL);

				// build up a string
				char temp[256];
				u32 len(0);
				if (curitem.m_symbol.length() < (m_divider - 1))
				{
					memset(&temp[len], ' ', m_divider - 1 - curitem.m_symbol.length());
					len += m_divider - 1 - curitem.m_symbol.length();
				}

				memcpy(&temp[len], curitem.m_symbol.c_str(), curitem.m_symbol.length());
				len += curitem.m_symbol.length();

				temp[len++] = ' ';
				temp[len++] = ' ';

				memcpy(&temp[len], valstr.c_str(), curitem.value_length());
				len += curitem.value_length();

				temp[len++] = ' ';
				temp[len] = 0;

				// copy data
				for (u32 effcol = m_topleft.x; (col < m_visible.x) && (effcol < len); ++dest, ++col)
				{
					dest->byte = temp[effcol++];
					dest->attrib = attrib | ((effcol <= m_divider) ? DCA_ANCILLARY : DCA_NORMAL);

					if (m_cursor_visible && index == m_cursor.y && (effcol - 1) == m_cursor.x)
						dest->attrib |= DCA_SELECTED;
				}
			}
		}

		// fill the rest with blanks
		while (visible && (col < m_visible.x))
		{
			dest->byte = ' ';
			dest->attrib = DCA_NORMAL;
			dest++;
			col++;
		}
	}

	// remember the last update
	m_last_update = total_cycles;
}


//-------------------------------------------------
//  view_char - handle a character typed within
//  the current view
//-------------------------------------------------

void debug_view_state::view_char(int chval)
{
	bool update_all_disasm_views = false;

	if (m_cursor_visible == false)
		return;

	// handle the incoming key
	switch (chval)
	{
	case DCH_UP:
		if (m_cursor.y > m_registers_start)
		{
			m_cursor.y--;

			if (m_cursor.x > m_divider + m_state_list[m_cursor.y].value_length())
				m_cursor.x = m_divider + m_state_list[m_cursor.y].value_length();
		}
		break;

	case DCH_DOWN:
		if (m_cursor.y < m_state_list.size() - 1)
		{
			m_cursor.y++;

			if (m_cursor.x > m_divider + m_state_list[m_cursor.y].value_length())
				m_cursor.x = m_divider + m_state_list[m_cursor.y].value_length();
		}
		break;

	case DCH_CTRLLEFT:
	case DCH_HOME:
		m_cursor.x = m_divider + 1;
		break;

	case DCH_CTRLRIGHT:
	case DCH_END:
		m_cursor.x = m_divider + m_state_list[m_cursor.y].value_length();
		break;

	case DCH_PUP:
		m_cursor.y = m_registers_start;
		m_cursor.x = m_divider + 1;
		break;

	case DCH_PDOWN:
		m_cursor.y = m_state_list.size() - 1;
		m_cursor.x = m_divider + 1;
		break;

	default:
	{
		static const char hexvals[] = "0123456789abcdef";
		char *hexchar = (char *)strchr(hexvals, tolower(chval));
		if (hexchar == nullptr)
			break;

		state_item &curitem(m_state_list[m_cursor.y]);

		u64 data = curitem.value();
		u8 digit_pos = (curitem.value_length() - 1) - (m_cursor.x - (m_divider + 1));
		u8 shift = digit_pos * 4;

		data &= ~(u64(0x0f) << shift);
		data |= u64(hexchar - hexvals) << shift;

		debug_view_state_source const &source(downcast<debug_view_state_source const &>(*m_source));

		source.m_stateintf->set_state_int(curitem.index(), data);

		/* set flag to update all disassembly views if PC affected */
		if (curitem.m_symbol == "PC")
			update_all_disasm_views = true;

		// fall through to the right-arrow press
	}
	case DCH_RIGHT:
		if (m_cursor.x < m_divider + m_state_list[m_cursor.y].value_length())
			m_cursor.x++;
		break;

	case DCH_LEFT:
		if (m_cursor.x > m_divider + 1)
			m_cursor.x--;
		break;
	}

	// update state view
	force_update();

	// update all opened debug views to be in sync with PC
	if (update_all_disasm_views)
	{
		machine().debug_view().update_all(DVT_DISASSEMBLY);
		machine().debug_view().flush_osd_updates();
	}
}


//-------------------------------------------------
//  view_click - handle a mouse click within the
//  current view
//-------------------------------------------------

void debug_view_state::view_click(const int button, const debug_view_xy& pos)
{
	m_cursor = pos;

	/* send a cursor changed notification */
	begin_update();
	view_notify(VIEW_NOTIFY_CURSOR_CHANGED);
	m_update_pending = true;
	end_update();
}


//-------------------------------------------------
//  state_item - constructor
//-------------------------------------------------

debug_view_state::state_item::state_item(int index, const char *name, u8 valuechars)
	: m_lastval(0)
	, m_currval(0)
	, m_index(index)
	, m_vallen(valuechars)
	, m_symbol(name)
{
}


//-------------------------------------------------
//  update - update value and save previous
//-------------------------------------------------

void debug_view_state::state_item::update(u64 newval, bool save)
{
	if (save)
		m_lastval = m_currval;
	m_currval = newval;
}
