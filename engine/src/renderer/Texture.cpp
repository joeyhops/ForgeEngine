#include <forge/Texture.h>
#include <GL/glew.h>

namespace forge {

Texture::~Texture() {
  if (id) glDeleteTextures(1, &id);
}

void Texture::bind(unsigned int unit) const {
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_2D, id);
}

}
