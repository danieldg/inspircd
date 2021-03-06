/*       +------------------------------------+
 *       | Inspire Internet Relay Chat Daemon |
 *       +------------------------------------+
 *
 *  InspIRCd: (C) 2002-2011 InspIRCd Development Team
 * See: http://wiki.inspircd.org/Credits
 *
 * This program is free but copyrighted software; see
 *            the file COPYING for details.
 *
 * ---------------------------------------------------
 */

#include "inspircd.h"

/** Handle /UNLOADMODULE. These command handlers can be reloaded by the core,
 * and handle basic RFC1459 commands. Commands within modules work
 * the same way, however, they can be fully unloaded, where these
 * may not.
 */
class CommandUnloadmodule : public Command
{
 public:
	/** Constructor for unloadmodule.
	 */
	CommandUnloadmodule ( Module* parent) : Command(parent,"UNLOADMODULE",1) { flags_needed = 'o'; syntax = "<modulename>"; }
	/** Handle command.
	 * @param parameters The parameters to the comamnd
	 * @param pcnt The number of parameters passed to teh command
	 * @param user The user issuing the command
	 * @return A value from CmdResult to indicate command success or failure.
	 */
	CmdResult Handle(const std::vector<std::string>& parameters, User *user);
};

CmdResult CommandUnloadmodule::Handle (const std::vector<std::string>& parameters, User *user)
{
	if (parameters[0] == "cmd_unloadmodule.so" || parameters[0] == "cmd_loadmodule.so")
	{
		user->WriteNumeric(972, "%s %s :You cannot unload module loading commands!", user->nick.c_str(), parameters[0].c_str());
		return CMD_FAILURE;
	}
		
	Module* m = ServerInstance->Modules->Find(parameters[0]);
	if (m && ServerInstance->Modules->Unload(m))
	{
		ServerInstance->SNO->WriteGlobalSno('a', "MODULE UNLOADED: %s unloaded %s", user->nick.c_str(), parameters[0].c_str());
		user->WriteNumeric(973, "%s %s :Module successfully unloaded.",user->nick.c_str(), parameters[0].c_str());
	}
	else
	{
		user->WriteNumeric(972, "%s %s :%s",user->nick.c_str(), parameters[0].c_str(),
			m ? ServerInstance->Modules->LastError().c_str() : "No such module");
		return CMD_FAILURE;
	}

	return CMD_SUCCESS;
}

COMMAND_INIT(CommandUnloadmodule)
