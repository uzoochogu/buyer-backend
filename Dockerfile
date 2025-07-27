FROM drogonframework/drogon:latest

# Install additional dependencies
RUN apt-get update && apt-get install -y \
    git \
    cmake \
    build-essential \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

# Install vcpkg
RUN git clone https://github.com/Microsoft/vcpkg.git /opt/vcpkg \
    && /opt/vcpkg/bootstrap-vcpkg.sh

# Set vcpkg environment
ENV VCPKG_ROOT=/opt/vcpkg
ENV PATH="${VCPKG_ROOT}:${PATH}"

# Set working directory
WORKDIR /app

# Copy source code
COPY . .

# Install dependencies via vcpkg
RUN vcpkg install

# Configure and build
RUN cmake -B ./build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release

RUN cmake --build build --config Release --parallel

# Expose port
EXPOSE 5555

# Run the application
CMD ["./build/buyer-backend"]
