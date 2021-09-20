#!/usr/bin/env python3

import numpy as np
import matplotlib.pyplot as plt
import concurrent.futures
import queue
import lib.swt21
import sys

class Event:
	INPUT = 0

	def __init__(self, type, data):
		self.type = type
		self.data = data


class Main:
	def __init__(self):
		self.queue = queue.Queue()

	def read_input(self):
		while True:
			try:
				line = input('')
				self.queue.put(Event(Event.INPUT, line))
			except:
				return


	def run(self):

		if(len(sys.argv) < 2):
			print('Usage: {} <serial port>'.format(sys.argv[0]))
			return

		port = sys.argv[1]

		executor = concurrent.futures.ThreadPoolExecutor()
		executor.submit(self.read_input)

		dev = lib.swt21.SWT21(port, 2000000, self.queue)

		while True:
			event = self.queue.get()

			if(type(event) == Event):
				if(event.data == 'test'):
					import random

					t = range(100)
					values = [random.randint(0,100) for x in t]

					plt.figure(1)
					plt.clf()
					plt.title('Test')
					plt.plot(t, values)
					plt.show()

				else:
					dev.send_command((event.data + '\n').encode())

			elif(type(event) == lib.swt21.Event):
				if(event.type == lib.swt21.Event.RESPONSE):
					print(event.data.decode().strip())

				elif(event.type == lib.swt21.Event.COMMAND):
					command = event.data

					if(command[0] in ['CAN RX', 'LIN RX', 'UART']):
						print(command[1].decode())

					elif(command[0] == 'ADC trig'):
#						print(command[1])
						clk = command[1][0]
						m = command[1][1]
						start = command[1][2]
						end = command[1][3]
						values = command[1][4]

						dt = 1e6/clk # Sample time measured in µs

						t = [i*dt for i in range(len(values))]

						plt.figure(1)
						plt.clf()
						plt.title('')
						plt.plot(t, values)
						plt.grid()
						plt.xlabel('Time (µs)')
						plt.ylabel('Raw ADC value (0-255)')
						plt.show()

			else:
				print(event)


if(__name__ == '__main__'):

	main = Main()
	main.run()
