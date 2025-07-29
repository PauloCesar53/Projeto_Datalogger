import pandas as pd
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D # Importar para gráficos 3D


try:
    df = pd.read_csv('sensor_data.csv')
except FileNotFoundError:
      
    from io import StringIO
    data_str = """amostra,AccX,AccY,AccZ,GyroX,GyroY,GyroZ
1,0.217,0.001,0.974,-11.290,9.252,-6.969
2,0.322,-0.021,0.989,-9.099,12.267,-53.229
3,0.356,-0.152,1.109,-162.321,15.580,66.206
4,0.051,0.235,0.970,29.046,6.672,-99.710
5,0.237,-0.089,0.890,90.985,-38.221,-5.641
6,0.082,-0.516,0.465,-169.084,-0.855,25.389
7,-0.089,1.071,1.214,-15.763,96.931,-42.305
8,-0.311,-0.469,1.250,-144.634,-10.985,-32.870
9,0.173,-0.444,0.795,21.168,-54.885,0.924
10,0.483,-0.446,1.078,201.962,-17.321,-7.084
11,-0.087,1.130,0.844,54.618,39.504,13.802
12,0.189,-0.366,1.004,-65.611,-142.969,-112.847
13,-0.110,1.074,0.682,-28.542,91.916,109.664
14,0.272,-0.135,0.720,38.084,-64.382,-55.763
15,-0.183,-0.156,1.274,-33.855,250.130,95.298
"""
    df = pd.read_csv(StringIO(data_str))


# 2. Definir as colunas para os eixos
x = df['amostra']
y = df['AccX']
z = df['AccY'] # AccY será o eixo Z no gráfico 3D

# 3. Plotar o gráfico 3D
fig = plt.figure(figsize=(10, 7)) # Cria uma figura
ax = fig.add_subplot(111, projection='3d') # Adiciona um subplot 3D

ax.plot(x, y, z, marker='o', linestyle='-', color='b') # Plota os dados

# Configurar os rótulos dos eixos e o título
ax.set_xlabel('Amostra')
ax.set_ylabel('AccX')
ax.set_zlabel('AccY')
ax.set_title('Gráfico 3D: Amostra vs AccX vs AccY')

# Exibir o grid
ax.grid(True)

# Mostrar o gráfico
plt.show()

# Se você quiser também um gráfico 2D separado, como você tinha inicialmente,
# mas com as colunas corretas (Amostra vs AccX):
print("\n--- Gráfico 2D: Amostra vs AccX ---")
plt.figure(figsize=(8, 5))
plt.plot(x, y, 'ro-') # 'ro-' significa pontos vermelhos e linha
plt.title('Gráfico 2D: Amostra vs AccX')
plt.xlabel('Amostra')
plt.ylabel('AccX')
plt.grid(True)
plt.show()