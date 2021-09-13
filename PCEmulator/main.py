"""
A calculator app written in Python using Kivy.

Initial version found at https://www.geeksforgeeks.org/how-to-make-calculator-using-kivy-python/

Tweaked here for extended functionality, improved design and a higher degree of testability.
"""

__version__ = '0.2'

# According to kivy docs, Config.set should be done before any other kivy modules are imported
# Makes window resizable to fit different screen dimensions
from kivy.config import Config
Config.set('graphics', 'resizable', 1)

# Basic imports: the App object and the GridLayout that will hold all parts together
import kivy
from kivy.app import App 
from kivy.uix.gridlayout import GridLayout

# Makes sure the installation running the app has a sufficient version of kivy, to prevent unexpected errors
kivy.require('1.9.0')
	

class CalcGridLayout(GridLayout):
    """
    The layout is a container for other widgets: the buttons and the display. Those "child widgets" are 
    defined in the .kv file.
    """
    def calculate(self, calculation):
        """
        The "bridge" between the math functionality and user inputs. Updates the display with a result.
        """
        if calculation:
            try:
                self.display.text = calculate_execution(calculation)
            except Exception:
                self.display.text = "Error"


def calculate_execution(calculation):
    """Evaluates the math expression. 
    
    This is separate from the App itself so that it can be tested separately."""
    result = str(eval(calculation))
    float_result = round(float(result), 6)
    if float_result.is_integer():
        result = str(int(float_result))
    else:
        result = str(float_result)
    return result


class CalculatorApp(App):
    """
    The App object is responsible for more "meta-level" functionality of the app, like how it behaves on startup, 
    what icon to represent the app with, how to handle "pause" events etc.    
    """
    def build(self):
        """Called once on startup, returns the root widget of the app"""
        self.icon = 'data/icon.png'
        return CalcGridLayout()

# Create app and run it
if __name__ == '__main__':
    calcApp = CalculatorApp()
    calcApp.run()
