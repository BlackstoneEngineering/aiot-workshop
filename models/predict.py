from keras.models import load_model
import numpy as np
model = load_model('./models/workshop_model.h5')

X=[[50, 48.981, 47.01, 49, 51, 47.6, 50, 49.8, 47.8, 48.0]]

X=np.array(X)

classes = model.predict(X)
print(classes)
