import numpy as np
import matplotlib.pyplot as plt

data = np.loadtxt('adc_data0.txt')

x = data[:,0]
y = data[:,1]
plt.plot(x,y,'ro-.')
plt.title("Gráfico dos dados coletados")
plt.xlabel("tempo")
plt.ylabel("Amplitude")
plt.grid()
plt.show()