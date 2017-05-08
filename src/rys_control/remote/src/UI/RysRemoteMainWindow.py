from PyQt5 import QtWidgets, QtCore, QtGui
from Gamepad import GamepadBridge
from UI.Layouts import Ui_RysRemoteMainWindow
from ROS import RosBridge

class RysRemoteMainWindow(QtWidgets.QMainWindow):
	"""
	Main window class for RysRemote.
	Inherits from QMainWindow.
	"""

	def __init__(self, node, parent = None):
		super(RysRemoteMainWindow, self).__init__(parent)

		self.node = node

		self.throttle = 0
		self.rotation = 0
		self.gamepadID = -1
		self.throttleAxis = -1
		self.rotationAxis = -1

		self.ui = Ui_RysRemoteMainWindow()
		self.ui.setupUi(self)

		self.ui.throttleComboBox.currentTextChanged.connect(self.throttleAxisChangedHandler)
		self.ui.rotationComboBox.currentTextChanged.connect(self.rotationAxisChangedHandler)
		self.ui.gamepadComboBox.currentIndexChanged.connect(self.gamepadChangedHandler)

		self.gamepadScene = QtWidgets.QGraphicsScene(self)
		self.ui.steeringGraphicsView.setScene(self.gamepadScene)
		self.ui.steeringGraphicsView.fitInView(self.gamepadScene.sceneRect(), QtCore.Qt.KeepAspectRatio)

		self.gamepadBridge = GamepadBridge(self)
		self.gamepadBridge.gamepadAxisChanged.connect(self.gamepadAxisChangedHandler)
		self.gamepadBridge.gamepadButtonChanged.connect(self.gamepadButtonChangedHandler)
		self.gamepadBridge.gamepadListUpdated.connect(self.gamepadListUpdatedHandler)
		self.gamepadBridge.start()

		self.rosBridge = RosBridge(node, self)
		self.rosBridge.rollChanged.connect(self.rollChangedHandler)
		self.rosBridge.sonarChanged.connect(self.sonarChangedHandler)
		self.rosBridge.start()

		self.adjustSize()

	""" UI event handlers """

	def throttleAxisChangedHandler(self, event):
		try:
			self.throttleAxis = int(event)
		except ValueError:
			self.throttleAxis = -1
		self.throttle = 0

	def rotationAxisChangedHandler(self, event):
		try:
			self.rotationAxis = int(event)
		except ValueError:
			self.rotationAxis = -1
		self.rotation = 0

	def gamepadChangedHandler(self, index):
		try:
			self.gamepadID = int(index)
		except ValueError:
			self.gamepadID = -1

	""" Gamepad bridge event handlers """

	def gamepadAxisChangedHandler(self, gamepadAxisEvent):
		gamepadID = gamepadAxisEvent.gamepadID
		axis = gamepadAxisEvent.axis
		value = gamepadAxisEvent.value
		print("axis event: joy %d, axis %d, value %f" % (gamepadID, axis, value))

		update = False
		if gamepadID is self.gamepadID:
			if axis is self.throttleAxis:
				self.throttle = value
				update = True
			elif axis is self.rotationAxis:
				self.rotation = value
				update = True

		if update:
			self.rosBridge.setSteering(self.throttle, self.rotation)
			self.repaintSteering()

	def gamepadButtonChangedHandler(self, gamepadButtonEvent):
		gamepadID = gamepadButtonEvent.gamepadID
		button = gamepadButtonEvent.button
		value = gamepadButtonEvent.value
		print("butt event: joy %d, butt %d, value %f" % (gamepadID, button, value))

	def gamepadListUpdatedHandler(self, gamepadList):
		self.gamepadID = -1

		for i in range(self.ui.gamepadComboBox.count()):
			self.ui.gamepadComboBox.removeItem(0)

		if len(gamepadList) > 0:
			for i in range(len(gamepadList)):
				self.ui.gamepadComboBox.addItem(gamepadList[i], i)
			self.ui.gamepadComboBox.setEnabled(True)
			self.ui.throttleComboBox.setEnabled(True)
			self.ui.rotationComboBox.setEnabled(True)

			self.gamepadID = 0
			self.throttleAxis = -1
			self.rotationAxis = -1
		else:
			self.ui.gamepadComboBox.setEnabled(False)
			self.ui.throttleComboBox.setEnabled(False)
			self.ui.rotationComboBox.setEnabled(False)
			self.gamepadID = -1
			self.throttleAxis = -1
			self.rotationAxis = -1

	""" ROS event handlers """

	def rollChangedHandler(self, value):
		self.ui.rollDial.setValue(int(value))
		self.ui.rollValueLabel.setText(str(value))

	def sonarChangedHandler(self, front, back, top):
		self.ui.sonarFrontBar.setValue(front)
		self.ui.sonarBackBar.setValue(back)
		self.ui.sonarTopBar.setValue(top)

	""" Miscelanneous event handlers """

	def quitEventHandler(self):
		# "Join" all QThreads
		# Gamepad bridge
		if self.gamepadBridge.isRunning():
			loop = QtCore.QEventLoop()
			self.gamepadBridge.finished.connect(loop.quit)
			self.gamepadBridge.stopExecution()
			loop.exec_()

		# Ros bridge
		if self.rosBridge.isRunning():
			loop = QtCore.QEventLoop()
			self.rosBridge.finished.connect(loop.quit)
			self.rosBridge.stopExecution()
			loop.exec_()

	""" Other methods """

	def repaintSteering(self):
		self.gamepadScene.clear()
		height = self.ui.steeringGraphicsView.size().height()
		width = self.ui.steeringGraphicsView.size().width()
		self.gamepadScene.setSceneRect(0.0, 0.0, width, height)

		brush = QtGui.QBrush(QtGui.QColor(200, 0, 0), QtCore.Qt.SolidPattern)
		pen = QtGui.QPen(brush, 5.0)

		y = (self.throttle * 0.5 + 0.5) * height
		x = (self.rotation * 0.5 + 0.5) * width
		dotSize = 10
		self.gamepadScene.addEllipse(x - dotSize / 2, y - dotSize / 2, dotSize, dotSize, pen, brush)
