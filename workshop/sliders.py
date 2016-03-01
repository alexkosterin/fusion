import sys, getopt
from PyQt4 import QtCore, QtGui
from mb import *

window = None

def on_receive(m, i):
    global window
    window.valueSpinBox.setValue(i)

class SlidersGroup(QtGui.QGroupBox):
    valueChanged = QtCore.pyqtSignal(int)

    def __init__(self, orientation, title, parent=None):
        super(SlidersGroup, self).__init__(title, parent)

        self.slider = QtGui.QSlider(orientation)
        self.slider.setFocusPolicy(QtCore.Qt.StrongFocus)
        self.slider.setTickPosition(QtGui.QSlider.TicksBothSides)
        self.slider.setTickInterval(10)
        self.slider.setSingleStep(1)

        self.scrollBar = QtGui.QScrollBar(orientation)
        self.scrollBar.setFocusPolicy(QtCore.Qt.StrongFocus)

        self.dial = QtGui.QDial()
        self.dial.setFocusPolicy(QtCore.Qt.StrongFocus)

        self.slider.valueChanged.connect(self.scrollBar.setValue)
        self.scrollBar.valueChanged.connect(self.dial.setValue)
        self.dial.valueChanged.connect(self.slider.setValue)

        if orientation == QtCore.Qt.Horizontal:
            direction = QtGui.QBoxLayout.TopToBottom
        else:
            direction = QtGui.QBoxLayout.LeftToRight

        slidersLayout = QtGui.QBoxLayout(direction)
        slidersLayout.addWidget(self.slider)
        slidersLayout.addWidget(self.scrollBar)
        slidersLayout.addWidget(self.dial)
        self.setLayout(slidersLayout)    

    def setValue(self, value):    
        self.slider.setValue(value)    

    def setMinimum(self, value):    
        self.slider.setMinimum(value)
        self.scrollBar.setMinimum(value)
        self.dial.setMinimum(value)    

    def setMaximum(self, value):    
        self.slider.setMaximum(value)
        self.scrollBar.setMaximum(value)
        self.dial.setMaximum(value)    

    def invertAppearance(self, invert):
        self.slider.setInvertedAppearance(invert)
        self.scrollBar.setInvertedAppearance(invert)
        self.dial.setInvertedAppearance(invert)    

    def invertKeyBindings(self, invert):
        self.slider.setInvertedControls(invert)
        self.scrollBar.setInvertedControls(invert)
        self.dial.setInvertedControls(invert)

class Window(QtGui.QWidget):
    def __init__(self, opt_name, opt_host, opt_port, opt_profile, opt_reader, opt_excl):
        super(Window, self).__init__()

        self.horizontalSliders = SlidersGroup(QtCore.Qt.Horizontal, opt_msg, self)
        self.verticalSliders = SlidersGroup(QtCore.Qt.Vertical, opt_msg, self)

        self.stackedWidget = QtGui.QStackedWidget()
        self.stackedWidget.addWidget(self.horizontalSliders)
        self.stackedWidget.addWidget(self.verticalSliders)

        self.createControls("Controls")

        self.horizontalSliders.valueChanged.connect(self.verticalSliders.setValue)
        self.verticalSliders.valueChanged.connect(self.valueSpinBox.setValue)
        self.valueSpinBox.valueChanged.connect(self.horizontalSliders.setValue)

        layout = QtGui.QHBoxLayout()
        layout.addWidget(self.controlsGroup)
        layout.addWidget(self.stackedWidget)
        self.setLayout(layout)

        self.minimumSpinBox.setValue(-100)
        self.maximumSpinBox.setValue(100)

        self.setWindowTitle(opt_name)

        self.fusion=Client(opt_name)
        self.fusion.reg(profile=opt_profile, host=opt_host, port=opt_port)
        flags = 'r' if opt_reader else 'w'
        if opt_excl: flags += 'x' 
        self.m = self.fusion.mopen(opt_msg, flags)

        if opt_reader:
            self.fusion.sub(self.m, on_receive, type=float)
        else:
            self.horizontalSliders.dial.valueChanged.connect(self.pub)
            self.verticalSliders.dial.valueChanged.connect(self.pub)

        self.timer = QtCore.QTimer(self)
        self.timer.setSingleShot(False)
        self.timer.timeout.connect(self.dispatch)
        self.timer.start(1)

    def createControls(self, title):
        self.controlsGroup = QtGui.QGroupBox(title)

        minimumLabel = QtGui.QLabel("Minimum value:")
        maximumLabel = QtGui.QLabel("Maximum value:")
        valueLabel = QtGui.QLabel("Current value:")

        invertedAppearance = QtGui.QCheckBox("Inverted appearance")
        invertedKeyBindings = QtGui.QCheckBox("Inverted key bindings")

        self.minimumSpinBox = QtGui.QSpinBox()
        self.minimumSpinBox.setRange(-100, 100)
        self.minimumSpinBox.setSingleStep(1)

        self.maximumSpinBox = QtGui.QSpinBox()
        self.maximumSpinBox.setRange(-100, 100)
        self.maximumSpinBox.setSingleStep(1)

        self.valueSpinBox = QtGui.QSpinBox()
        self.valueSpinBox.setRange(-100, 100)
        self.valueSpinBox.setSingleStep(1)

        orientationCombo = QtGui.QComboBox()
        orientationCombo.addItem("Horizontal slider-like widgets")
        orientationCombo.addItem("Vertical slider-like widgets")

        orientationCombo.activated.connect(self.stackedWidget.setCurrentIndex)
        self.minimumSpinBox.valueChanged.connect(self.horizontalSliders.setMinimum)
        self.minimumSpinBox.valueChanged.connect(self.verticalSliders.setMinimum)
        self.maximumSpinBox.valueChanged.connect(self.horizontalSliders.setMaximum)
        self.maximumSpinBox.valueChanged.connect(self.verticalSliders.setMaximum)
        invertedAppearance.toggled.connect(self.horizontalSliders.invertAppearance)
        invertedAppearance.toggled.connect(self.verticalSliders.invertAppearance)
        invertedKeyBindings.toggled.connect(self.horizontalSliders.invertKeyBindings)
        invertedKeyBindings.toggled.connect(self.verticalSliders.invertKeyBindings)

        controlsLayout = QtGui.QGridLayout()
        controlsLayout.addWidget(minimumLabel, 0, 0)
        controlsLayout.addWidget(maximumLabel, 1, 0)
        controlsLayout.addWidget(valueLabel, 2, 0)
        controlsLayout.addWidget(self.minimumSpinBox, 0, 1)
        controlsLayout.addWidget(self.maximumSpinBox, 1, 1)
        controlsLayout.addWidget(self.valueSpinBox, 2, 1)
        controlsLayout.addWidget(invertedAppearance, 0, 2)
        controlsLayout.addWidget(invertedKeyBindings, 1, 2)
        controlsLayout.addWidget(orientationCombo, 3, 0, 1, 3)
        self.controlsGroup.setLayout(controlsLayout)

    def dispatch(self):
        self.fusion.dispatch(True, 0)

    def pub(self, v):
        self.fusion.pub(self.m, float(v))

def usage():  ##################################################################
  print >> sys.stderr, """PyQt reader/writer example. NOV(c) 2014
pingpong ...
  -h, --help       Print help and exit.
  -H, --host       Fusion address.
  -p, --port       Fusion port.
  -P, --profile    Profile to use.
  -r, --reader=MSG Reader
  -w, --writer=MSG Writer
  -x, --exclusive  Exclusive reader or writer
"""

if __name__ == '__main__':
    try:
        opts, args = getopt.getopt(sys.argv[1:], "hH:p:P:n:r:w:x", ["help", "host=", "port=", "profile=", "name=", "reader=", "writer=", "exclusive"])
    except getopt.GetoptError as err:
        print >> sys.stderr, str(err)
        usage()
        sys.exit(1)

    app = QtGui.QApplication(sys.argv)

    opt_host	= 'localhost';
    opt_port	= '3001'
    opt_profile	= ''
    opt_msg		= None
    opt_name	= None
    opt_reader	= False
    opt_excl	= False

    ## parse arguments ##
    for o, a in opts:
        if o in ("-h", "--help"):
            usage()
            sys.exit(0)
        elif o in ("-H", "--host"):
            opt_host = a
        elif o in ("-p", "--port"):
            opt_port = a
        elif o in ("-f", "--profile"):
            opt_profile = a
        elif o in ("-n", "--name"):
            opt_name = a
        elif o in ("-r", "--reader"):
            opt_reader = True
            opt_msg = a
        elif o in ("-w", "--writer"):
            opt_reader = False
            opt_msg = a
        elif o in ["-x", "--exclusive"]:
            opt_excl = not opt_excl

    if not opt_name:
        opt_name = "%s - %smsg:%s" % ("reader" if opt_reader else "writer", "exclusive " if opt_excl else "", opt_msg)

    window = Window(opt_name, opt_host, opt_port, opt_profile, opt_reader, opt_excl)
    window.show()
    sys.exit(app.exec_())
