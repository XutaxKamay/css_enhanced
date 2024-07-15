# encoding: utf-8
# gitversion.py -- waf plugin to get git version
# Copyright (C) 2018 a1batross
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

import subprocess
from waflib import Configure, Logs

def run_git(conf, argv):
	try:
		stdout = conf.cmd_and_log([conf.env.GIT[0]] + argv, cwd = conf.srcnode)
		data = stdout.strip()
	except Exception as e:
		Logs.debug(str(e))
		return None

	if len(data) == 0:
		return None

	return data

@Configure.conf
def get_git_version(conf):
	# try grab the current version number from git
	node = conf.srcnode.find_node('.git')

	if not node:
		Logs.debug('can\'t find .git in conf.srcnode')
		return None

	return run_git(conf, ['describe', '--dirty', '--always'])

@Configure.conf
def get_git_branch(conf):
	node = conf.srcnode.find_node('.git')

	if not node:
		Logs.debug('can\'t find .git in conf.srcnode')
		return None

	return run_git(conf, ['rev-parse', '--abbrev-ref', 'HEAD'])

def configure(conf):
	if not conf.find_program('git', mandatory = False):
		return

	conf.start_msg('Git commit hash')
	conf.env.GIT_VERSION = conf.get_git_version()
	conf.end_msg(conf.env.GIT_VERSION)

	conf.start_msg('Git branch')
	conf.env.GIT_BRANCH = conf.get_git_branch()
	conf.end_msg(conf.env.GIT_BRANCH)
