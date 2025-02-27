/*
 * 86Box A hypervisor and IBM PC system emulator that specializes in
 *      running old operating systems and software designed for IBM
 *      PC systems and compatibles from 1981 through fairly recent
 *      system designs based on the PCI bus.
 *
 *      This file is part of the 86Box distribution.
 *
 *      OpenGL renderer options for Qt
 *
 * Authors:
 *      Teemu Korhonen
 *
 *      Copyright 2022 Teemu Korhonen
 */

#include <QFile>
#include <QRegularExpression>
#include <QStringBuilder>

#include <stdexcept>

#include "qt_opengloptions.hpp"

extern "C" {
#include <86box/86box.h>
}

/* Default vertex shader. */
static const GLchar *vertex_shader = "\
in vec2 VertexCoord;\n\
in vec2 TexCoord;\n\
out vec2 tex;\n\
void main(){\n\
    gl_Position = vec4(VertexCoord, 0.0, 1.0);\n\
    tex = TexCoord;\n\
}\n";

/* Default fragment shader. */
static const GLchar *fragment_shader = "\
in vec2 tex;\n\
uniform sampler2D texsampler;\n\
out vec4 color;\n\
void main() {\n\
    color = texture(texsampler, tex);\n\
}\n";

OpenGLOptions::OpenGLOptions(QObject *parent, bool loadConfig)
    : QObject(parent)
{
    if (!loadConfig)
        return;

    /* Initialize with config. */
    m_vsync     = video_vsync != 0;
    m_framerate = video_framerate;

    m_renderBehavior = video_framerate == -1
        ? RenderBehaviorType::SyncWithVideo
        : RenderBehaviorType::TargetFramerate;

    m_filter = video_filter_method == 0
        ? FilterType::Nearest
        : FilterType::Linear;

    QString shaderPath(video_shader);

    if (shaderPath.isEmpty()) {
        addDefaultShader();
    } else {
        try {
            addShader(shaderPath);
        } catch (const std::runtime_error &) {
            /* Fallback to default shader */
            addDefaultShader();
        }
    }
}

void
OpenGLOptions::save() const
{
    video_vsync         = m_vsync ? 1 : 0;
    video_framerate     = m_renderBehavior == RenderBehaviorType::SyncWithVideo ? -1 : m_framerate;
    video_filter_method = m_filter == FilterType::Nearest ? 0 : 1;

    /* TODO: multiple shaders */
    auto path = m_shaders.first().path().toLocal8Bit();

    if (!path.isEmpty())
        strcpy(video_shader, path.constData());
    else
        video_shader[0] = '\0';
}

OpenGLOptions::FilterType
OpenGLOptions::filter() const
{
    /* Filter method is controlled externally */
    return video_filter_method == 0
        ? FilterType::Nearest
        : FilterType::Linear;
}

void
OpenGLOptions::setRenderBehavior(RenderBehaviorType value)
{
    m_renderBehavior = value;
}

void
OpenGLOptions::setFrameRate(int value)
{
    m_framerate = value;
}

void
OpenGLOptions::setVSync(bool value)
{
    m_vsync = value;
}

void
OpenGLOptions::setFilter(FilterType value)
{
    m_filter = value;
}

void
OpenGLOptions::addShader(const QString &path)
{
    QFile shader_file(path);

    if (!shader_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        throw std::runtime_error(
            QString(tr("Error opening \"%1\": %2"))
                .arg(path)
                .arg(shader_file.errorString())
                .toStdString());
    }

    auto shader_text = QString(shader_file.readAll());

    shader_file.close();

    QRegularExpression version("^\\s*(#version\\s+\\w+)", QRegularExpression::MultilineOption);

    auto match = version.match(shader_text);

    QString version_line("#version 130");

    if (match.hasMatch()) {
        /* Extract existing version and remove it. */
        version_line = match.captured(1);
        shader_text.remove(version);
    }

    if (QOpenGLContext::currentContext() && QOpenGLContext::currentContext()->isOpenGLES()) {
        /* Force #version 300 es (the default of #version 100 es is too old and too limited) */
        version_line = "#version 300 es";
    }
    auto shader = new QOpenGLShaderProgram(this);

    auto throw_shader_error = [path, shader](const QString &what) {
        throw std::runtime_error(
            QString(what % ":\n\n %2")
                .arg(path)
                .arg(shader->log())
                .toStdString());
    };

    if (!shader->addShaderFromSourceCode(QOpenGLShader::Vertex, version_line % "\n#extension GL_ARB_shading_language_420pack : enable\n" % "\n#define VERTEX\n" % shader_text))
        throw_shader_error(tr("Error compiling vertex shader in file \"%1\""));

    if (!shader->addShaderFromSourceCode(QOpenGLShader::Fragment, version_line % "\n#extension GL_ARB_shading_language_420pack : enable\n" % "\n#define FRAGMENT\n" % shader_text))
        throw_shader_error(tr("Error compiling fragment shader in file \"%1\""));

    if (!shader->link())
        throw_shader_error(tr("Error linking shader program in file \"%1\""));

    m_shaders << OpenGLShaderPass(shader, path);
}

void
OpenGLOptions::addDefaultShader()
{
    QString version = QOpenGLContext::currentContext() && QOpenGLContext::currentContext()->isOpenGLES()
        ? "#version 300 es\n"
        : "#version 130\n";

    auto shader = new QOpenGLShaderProgram(this);
    shader->addShaderFromSourceCode(QOpenGLShader::Vertex, version % vertex_shader);
    shader->addShaderFromSourceCode(QOpenGLShader::Fragment, version % fragment_shader);
    shader->link();
    m_shaders << OpenGLShaderPass(shader, QString());
}
