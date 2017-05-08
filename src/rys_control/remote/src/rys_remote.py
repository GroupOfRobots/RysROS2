#!/usr/bin/env python3

import rclpy
import sys
from UI import RysRemoteUI

def main(args = None):
	if args is None:
		args = sys.argv

	rclpy.init(args)
	node = rclpy.create_node('rys_remote')

	ui = RysRemoteUI(node)
	sys.exit(ui.exec_(args))

if __name__ == '__main__':
	main()
