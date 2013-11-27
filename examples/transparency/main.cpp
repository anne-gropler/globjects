#include <GL/glew.h>

#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>

#include <glow/Program.h>
#include <glow/Texture.h>
#include <glow/DebugMessageOutput.h>

#include <glowwindow/ContextFormat.h>
#include <glowwindow/Window.h>
#include <glowwindow/MainLoop.h>
#include <glowwindow/Context.h>
#include <glowwindow/WindowEventHandler.h>

#include <glowutils/UnitCube.h>
#include <glowutils/File.h>
#include <glowutils/Camera.h>
#include <glowutils/AbstractCoordinateProvider.h>
#include <glowutils/WorldInHandNavigation.h>
#include <glowutils/FileRegistry.h>
#include <glowutils/ScreenAlignedQuad.h>

#include "GlBlendAlgorithm.h"
#include "ABufferAlgorithm.h"

namespace {

struct CubeUniformAttributes {
    glm::vec3 position;
    glm::vec4 color;

};

} // anonymous namespace

class EventHandler : public glowwindow::WindowEventHandler, glowutils::AbstractCoordinateProvider {
private:
	glowutils::Camera* m_camera;
	glowutils::UnitCube* m_cube;
	glowutils::WorldInHandNavigation m_nav;
	glowutils::AxisAlignedBoundingBox m_aabb;
	glowutils::ScreenAlignedQuad* m_quad;
    std::vector<glow::TransparencyAlgorithm*> m_algos;

public:
	void initialize(glowwindow::Window & window) override {
		glow::DebugMessageOutput::enable();

		glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

        m_algos.push_back(new glow::GlBlendAlgorithm);
        m_algos.push_back(new glow::ABufferAlgorithm);
        for (auto& algo : m_algos) {
            algo->initialize();
        }

        m_cube = new glowutils::UnitCube;

		m_camera = new glowutils::Camera(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

		// Setup the screen aligned quad stuff
		glow::Program* quadProgram = new glow::Program();
        quadProgram->attach(glowutils::createShaderFromFile(GL_FRAGMENT_SHADER, "data/transparency/quad.frag"));
        quadProgram->attach(glowutils::createShaderFromFile(GL_VERTEX_SHADER, "data/transparency/quad.vert"));
        m_quad = new glowutils::ScreenAlignedQuad(quadProgram);

		m_aabb.extend(glm::vec3(-1.f, -0.5f, -10.5f));
		m_aabb.extend(glm::vec3(0.f, 0.5f, -5.5));

		m_nav.setCamera(m_camera);
		m_nav.setCoordinateProvider(this);
		m_nav.setBoundaryHint(m_aabb);

		CheckGLError();
	}

	void paintEvent(glowwindow::PaintEvent& event) override {
		int width = event.window()->width();
		int height = event.window()->height();

		const CubeUniformAttributes cubes[3] = {
			CubeUniformAttributes{ glm::vec3(0.0f, 0.0f, -10.0f), glm::vec4(1.0, 0.0, 0.0, 0.3) },
			CubeUniformAttributes{ glm::vec3(0.0f, -0.25f, -8.0f), glm::vec4(0.0, 1.0, 0.0, 0.3) },
			CubeUniformAttributes{ glm::vec3(0.0f, -0.5f, -6.0f), glm::vec4(0.0, 0.0, 1.0, 0.3) }
		};

        for (auto& algo : m_algos) {
            algo->draw([&](glow::Program* program) {
                for (int c = 0; c < 3; c++) {
                    program->setUniform("modelmatrix", glm::translate<float>(cubes[c].position));
                    program->setUniform("color", cubes[c].color);
                    m_cube->draw();
                }
            }, m_camera, width, height);
        }

        // STAGE2 - Draw the texture of each algorithm& onto the screen aligned quad
		glViewport(0, 0, width, height);
		
		glDisable(GL_DEPTH_TEST);
		CheckGLError();

		glDepthMask(GL_FALSE);
		CheckGLError();

		m_quad->program()->setUniform("one", 0);
		m_quad->program()->setUniform("two", 1);
        m_algos[0]->getOutput()->bind(GL_TEXTURE0);
        m_algos[1]->getOutput()->bind(GL_TEXTURE1);

		m_quad->draw();

		glEnable(GL_DEPTH_TEST);
		CheckGLError();

		glDepthMask(GL_TRUE);
		CheckGLError();
	}

	void resizeEvent(glowwindow::ResizeEvent & event) override {
		int width = event.width();
		int height = event.height();
        for (auto& algo : m_algos) {
            algo->resize(width, height);
        }		
	}

	virtual void idle(glowwindow::Window & window) override
	{
        window.repaint();
	}

	virtual const float depthAt(const glm::ivec2 & windowCoordinates) override
	{
		return glowutils::AbstractCoordinateProvider::depthAt(*m_camera, GL_DEPTH_COMPONENT, windowCoordinates);
	}

	virtual const glm::vec3 objAt(const glm::ivec2 & windowCoordinates) override
	{
		return unproject(*m_camera, static_cast<GLenum>(GL_DEPTH_COMPONENT), windowCoordinates);
	}

	virtual const glm::vec3 objAt(const glm::ivec2 & windowCoordinates, const float depth) override
	{
		return unproject(*m_camera, depth, windowCoordinates);
	}

	virtual const glm::vec3 objAt(const glm::ivec2 & windowCoordinates, const float depth, const glm::mat4 & viewProjectionInverted) override
	{
		return unproject(*m_camera, viewProjectionInverted, depth, windowCoordinates);
	}

	virtual void mousePressEvent(glowwindow::MouseEvent & event) override
	{
		switch (event.button())
		{
		case GLFW_MOUSE_BUTTON_LEFT:
			m_nav.panBegin(event.pos());
			event.accept();
			event.window()->repaint();
			break;

		case GLFW_MOUSE_BUTTON_RIGHT:
			m_nav.rotateBegin(event.pos());
			event.accept();
			event.window()->repaint();
			break;
		}
	}

	virtual void mouseMoveEvent(glowwindow::MouseEvent & event) override
	{
		switch (m_nav.mode())
		{
		case glowutils::WorldInHandNavigation::PanInteraction:
			m_nav.panProcess(event.pos());
			event.accept();
			event.window()->repaint();
			break;

		case glowutils::WorldInHandNavigation::RotateInteraction:
			m_nav.rotateProcess(event.pos());
			event.accept();
			event.window()->repaint();
		}
	}

	virtual void mouseReleaseEvent(glowwindow::MouseEvent & event) override
	{
		switch (event.button())
		{
		case GLFW_MOUSE_BUTTON_LEFT:
			m_nav.panEnd();
			event.accept();
			event.window()->repaint();
			break;

		case GLFW_MOUSE_BUTTON_RIGHT:
			m_nav.rotateEnd();
			event.accept();
			event.window()->repaint();
			break;
		}
	}

	virtual void keyPressEvent(glowwindow::KeyEvent & event) override
	{
		const float d = 0.08f;

		switch (event.key())
		{
		case GLFW_KEY_F5:
			glowutils::FileRegistry::instance().reloadAll();
			break;
		}
	}

};

int main(int argc, char* argv[]) {

	glowwindow::ContextFormat format;
	format.setVersion(4, 3);
	format.setDepthBufferSize(16);
	//format.setSamples(4);

	glowwindow::Window window;

	if (!window.create(format, "Transparency")) return 1;
	window.context()->setSwapInterval(glowwindow::Context::VerticalSyncronization);
	window.setEventHandler(new EventHandler());
	window.show();
	return glowwindow::MainLoop::run();
	
}
