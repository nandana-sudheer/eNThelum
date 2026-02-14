# Use an official lightweight Python image
FROM python:3.11-slim

# Set the working directory inside the container
WORKDIR /app

# Copy the requirements file first to leverage Docker cache
# (We will create this file in the next step)
COPY requirements.txt .

# Install dependencies
RUN pip install --no-cache-dir -r requirements.txt

# Copy the rest of the application code
COPY . .

# Create the instance folder for the SQLite database
# This ensures Flask has permissions to write the .db file
RUN mkdir -p instance

# Expose the port Flask will run on
EXPOSE 5000

# Use Gunicorn for production instead of the Flask dev server
CMD ["gunicorn", "--bind", "0.0.0.0:5000", "app:app"]