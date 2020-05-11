/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "script_tool_frame.h"

#include "../bauer/script_tool_manager.h"
#include "../dataobj/tabfile.h"
#include "../dataobj/translator.h"
#include "../simdebug.h"
#include "../simtool.h"
#include "../sys/simsys.h"
#include "../utils/cbuffer_t.h"

char script_tool_frame_t::executed_script_name[PATH_MAX];

script_tool_frame_t::script_tool_frame_t() : savegame_frame_t(NULL, true, NULL, false)
{
	static cbuffer_t pakset_script_tool;
	static cbuffer_t addons_script_tool;

	pakset_script_tool.clear();
	pakset_script_tool.printf("%stool/", env_t::program_dir, env_t::objfilename.c_str());

	addons_script_tool.clear();
	addons_script_tool.printf("%stool/", env_t::objfilename.c_str());

	if (env_t::default_settings.get_with_private_paks()) {
		this->add_path(addons_script_tool);
	}
	this->add_path(pakset_script_tool);

	set_name(translator::translate("Load script tool"));
	set_focus(NULL);
}


/**
 * Action, started after button pressing.
 */
bool script_tool_frame_t::item_action(const char *fullpath)
{
	strcpy(executed_script_name, fullpath);
	if(  script_tool_manager_t::is_two_click_tool(executed_script_name)  ) {
		tool_exec_two_click_script_t* tt = static_cast<tool_exec_two_click_script_t*>(tool_t::general_tool[TOOL_EXEC_TWO_CLICK_SCRIPT]);
		if(  !tt  ) {
			dbg->error("script_tool_frame_t::item_action", "cannot get tool object.");
		}
		script_tool_manager_t::load_tool(tt, tt, executed_script_name, executed_script_name);
		tt->enable_restart();
		welt->set_tool( tt, welt->get_active_player() );
	} else {
		tool_exec_script_t* ot = static_cast<tool_exec_script_t*>(tool_t::general_tool[TOOL_EXEC_SCRIPT]);
		if(  !ot  ) {
			dbg->error("script_tool_frame_t::item_action", "cannot get tool object.");
		}
		script_tool_manager_t::load_tool(ot, ot, executed_script_name, executed_script_name);
		ot->enable_restart();
		welt->set_tool( ot, welt->get_active_player() );
	}
	return true;
}


const char *script_tool_frame_t::get_info(const char *filename)
{
	static char info[PATH_MAX];
	// try to get tool title from description.tab
	sprintf( info, "%s/description.tab", filename );
	tabfile_t file;
	if (  file.open(info)  ) {
		tabfileobj_t contents;
		file.read( contents );
		const char* title = contents.get_string("title", "");
		if(  strcmp(title, "")!=0  ) {
			// title is properly defined.
			strcpy(info, title);
			return info;
		}
	}
	
	sprintf(info,"%s",this->get_filename(filename, false).c_str());
	return info;
}


bool script_tool_frame_t::check_file( const char *filename, const char * )
{
	char buf[PATH_MAX];
	sprintf( buf, "%s/tool.nut", filename );
	if (FILE* const f = dr_fopen(buf, "r")) {
		fclose(f);
		return true;
	}
	return false;
}
