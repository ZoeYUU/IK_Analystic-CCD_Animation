#include "stdafx.h"
#define _CRT_SECURE_NO_DEPRECATE
#include <stdio.h>
//Some Windows Headers (For Time, IO, etc.)
#include <windows.h>
#include <mmsystem.h>
#include <GL/glew.h>
#include <GL/freeglut.h>
#include <iostream>
#include "maths_funcs.h"
#include "skeleton.h"
#include "Hand.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <stdlib.h>

// Macro for indexing vertex buffer
#define BUFFER_OFFSET(i) ((char *)NULL + (i))
#define M_PI       3.14159265358979323846   // pi
#define ONE_DEG_IN_RAD (2.0 * M_PI) / 360.0 // 0.017444444
#define ONE_RAD_IN_DEG 57.2957795
#define FROZE_TIME 5 //for frame1/2/3 IK Analystic
#define MAX_TRY 9 //CCD
#define FREEZE_TIME 10.0 //for fram4 hand CCD

using namespace std;

//screen
int width = 1200;
int height = 900;

//vbo object points count
int pointCount = 0;
//vao index
unsigned int vao = 0;

//shaders
GLuint shaderTx;
GLuint shaderRed;
GLuint tex;

//event response
mat4 local1T = identity_mat4();
mat4 local1R = identity_mat4();
mat4 local1S = identity_mat4();
GLfloat rotatez = 0.0f; //rotation of child teapot around self(left)
mat4 R = identity_mat4();
//unit rotation
float roDeg = 1.0;
float roGib = 1.0;
//keyboard
bool key_x_pressed = false;
bool key_z_pressed = false;
bool key_c_pressed = false;
bool key_v_pressed = false;
bool key_b_pressed = false;
bool key_n_pressed = false;
bool key_d_pressed = false;
bool key_f_pressed = false;
bool key_g_pressed = false;
bool key_h_pressed = false;
bool key_s_pressed = false;
bool key_a_pressed = false;
bool key_w_pressed = false;
bool key_q_pressed = false;

//IK
vec3 goals[3] = { vec3(2.0, -1.0, -2.0),vec3(3.0, -1.0, 3.0), vec3(2.5, 0.0, 3.0) };

float armUnit = 0.8; // length unit in blender
float L1 = 1.4*armUnit; // upperArm length
float L2 = 2.0*armUnit; // lowerArm length+hand
float B1 = 0.9; //shoulder width


				//spline
				//delta t, see update scene
float t = 0.0;
int frozeCount = 0;

// IK CCD hand 
vec3 goal = vec3(3.0, 0.0, 3.0); // 2-D no .y = -0.5

float L3 = 1.6*armUnit; // 1st finger length
float L4 = 1.25*armUnit; // 2nd finger length+hand
float L5 = 1.1*armUnit; // 3rd finger length+hand
float L0 = 2.0*armUnit; //palm length

int tried = 0; //count for how many times have tried
int max_try = 0;
float th = 0.0; //hand's frame count




#pragma region TEXTURE_FUNCTIONS
bool load_texture(const char *file_name, GLuint *tex) {
	int x, y, n;
	int force_channels = 4;
	unsigned char *image_data = stbi_load(file_name, &x, &y, &n, force_channels);
	if (!image_data) {
		fprintf(stderr, "ERROR: could not load %s\n", file_name);
		return false;
	}
	// NPOT check
	if ((x & (x - 1)) != 0 || (y & (y - 1)) != 0) {
		fprintf(stderr, "WARNING: texture %s is not power-of-2 dimensions\n",
			file_name);
	}
	int width_in_bytes = x * 4;
	unsigned char *top = NULL;
	unsigned char *bottom = NULL;
	unsigned char temp = 0;
	int half_height = y / 2;

	for (int row = 0; row < half_height; row++) {
		top = image_data + row * width_in_bytes;
		bottom = image_data + (y - row - 1) * width_in_bytes;
		for (int col = 0; col < width_in_bytes; col++) {
			temp = *top;
			*top = *bottom;
			*bottom = temp;
			top++;
			bottom++;
		}
	}
	glGenTextures(1, tex);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, *tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, x, y, 0, GL_RGBA, GL_UNSIGNED_BYTE,
		image_data);
	glGenerateMipmap(GL_TEXTURE_2D);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	GLfloat max_aniso = 0.0f;
	glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_aniso);
	// set the maximum!
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, max_aniso);
	return true;
}
#pragma endregion TEXTURE_FUNCTIONS


// Shader Functions- click on + to expand
#pragma region SHADER_FUNCTIONS

// Create a NULL-terminated string by reading the provided file
char* readShaderSource(const char* shaderFile) {
	FILE* fp = fopen(shaderFile, "rb"); //!->Why does binary flag "RB" work and not "R"... wierd msvc thing?

	if (fp == NULL) { return NULL; }

	fseek(fp, 0L, SEEK_END);
	long size = ftell(fp);

	fseek(fp, 0L, SEEK_SET);
	char* buf = new char[size + 1];
	fread(buf, 1, size, fp);
	buf[size] = '\0';

	fclose(fp);

	return buf;
}


static void AddShader(GLuint ShaderProgram, const char* pShaderText, GLenum ShaderType)
{
	// create a shader object
	GLuint ShaderObj = glCreateShader(ShaderType);

	if (ShaderObj == 0) {
		fprintf(stderr, "Error creating shader type %d\n", ShaderType);
		exit(0);
	}
	const char* pShaderSource = readShaderSource(pShaderText);

	// Bind the source code to the shader, this happens before compilation
	glShaderSource(ShaderObj, 1, (const GLchar**)&pShaderSource, NULL);
	// compile the shader and check for errors
	glCompileShader(ShaderObj);
	GLint success;
	// check for shader related errors using glGetShaderiv
	glGetShaderiv(ShaderObj, GL_COMPILE_STATUS, &success);
	if (!success) {
		GLchar InfoLog[1024];
		glGetShaderInfoLog(ShaderObj, 1024, NULL, InfoLog);
		fprintf(stderr, "Error compiling shader type %d: '%s'\n", ShaderType, InfoLog);
		exit(1);
	}
	// Attach the compiled shader object to the program object
	glAttachShader(ShaderProgram, ShaderObj);
}

GLuint CompileShaders(GLuint shaderProgramID, const char *vs, const char *fs)
{
	//Start the process of setting up our shaders by creating a program ID
	//Note: we will link all the shaders together into this ID
	shaderProgramID = glCreateProgram();
	if (shaderProgramID == 0) {
		fprintf(stderr, "Error creating shader program\n");
		exit(1);
	}

	// Create two shader objects, one for the vertex, and one for the fragment shader
	AddShader(shaderProgramID, vs, GL_VERTEX_SHADER);
	AddShader(shaderProgramID, fs, GL_FRAGMENT_SHADER);

	GLint Success = 0;
	GLchar ErrorLog[1024] = { 0 };
	// After compiling all shader objects and attaching them to the program, we can finally link it
	glLinkProgram(shaderProgramID);
	// check for program related errors using glGetProgramiv
	glGetProgramiv(shaderProgramID, GL_LINK_STATUS, &Success);
	if (Success == 0) {
		glGetProgramInfoLog(shaderProgramID, sizeof(ErrorLog), NULL, ErrorLog);
		fprintf(stderr, "Error linking shader program: '%s'\n", ErrorLog);
		exit(1);
	}

	// program has been successfully linked but needs to be validated to check whether the program can execute given the current pipeline state
	glValidateProgram(shaderProgramID);
	// check for program related errors using glGetProgramiv
	glGetProgramiv(shaderProgramID, GL_VALIDATE_STATUS, &Success);
	if (!Success) {
		glGetProgramInfoLog(shaderProgramID, sizeof(ErrorLog), NULL, ErrorLog);
		fprintf(stderr, "Invalid shader program: '%s'\n", ErrorLog);
		exit(1);
	}
	// Finally, use the linked shader program
	// Note: this program will stay in effect for all draw calls until you replace it with another or explicitly disable its use
	glUseProgram(shaderProgramID);
	return shaderProgramID;
}
#pragma endregion SHADER_FUNCTIONS


#pragma region VBO_FUNCTIONS
// VBO Functions - following Anton's code
// https://github.com/capnramses/antons_opengl_tutorials_book/tree/master/20_normal_mapping


int generateObjectBufferTeapot(const char *file_name) {

	int point_count = 0;
	float *points = NULL;
	float *normals = NULL;
	float *texcoords = NULL;
	float *vtans = NULL;

	Assimp::Importer importer;

	const aiScene* scene = importer.ReadFile(file_name,
		aiProcess_Triangulate | aiProcess_CalcTangentSpace);

	if (!scene) {
		fprintf(stderr, "ERROR: reading mesh %s\n", file_name);
		return 0;
	}
	fprintf(stderr, "reading mesh");
	printf("  %i animations\n", scene->mNumAnimations);
	printf("  %i cameras\n", scene->mNumCameras);
	printf("  %i lights\n", scene->mNumLights);
	printf("  %i materials\n", scene->mNumMaterials);
	printf("  %i meshes\n", scene->mNumMeshes);
	printf("  %i textures\n", scene->mNumTextures);

	/* get first mesh in file only */
	const aiMesh *mesh = scene->mMeshes[0];
	printf("    %i vertices in mesh[0]\n", mesh->mNumVertices);

	/* pass back number of vertex points in mesh */
	point_count = mesh->mNumVertices;
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);


	if (mesh->HasPositions()) {

		points = (float *)malloc(point_count * 3 * sizeof(float));
		for (int i = 0; i < point_count; i++) {
			const aiVector3D *vp = &(mesh->mVertices[i]);
			points[i * 3] = (float)vp->x;
			points[i * 3 + 1] = (float)vp->y;
			points[i * 3 + 2] = (float)vp->z;
		}

		GLuint vp_vbo = 0;
		glGenBuffers(1, &vp_vbo);
		glBindBuffer(GL_ARRAY_BUFFER, vp_vbo);
		glBufferData(GL_ARRAY_BUFFER, 3 * point_count * sizeof(float), points, GL_STATIC_DRAW);

		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);

		free(points);

	}
	if (mesh->HasNormals()) {
		normals = (float *)malloc(point_count * 3 * sizeof(float));
		for (int i = 0; i < point_count; i++) {
			const aiVector3D *vn = &(mesh->mNormals[i]);
			normals[i * 3] = (float)vn->x;
			normals[i * 3 + 1] = (float)vn->y;
			normals[i * 3 + 2] = (float)vn->z;
		}

		GLuint vn_vbo = 0;
		glGenBuffers(1, &vn_vbo);
		glBindBuffer(GL_ARRAY_BUFFER, vn_vbo);
		glBufferData(GL_ARRAY_BUFFER, 3 * point_count * sizeof(float), normals, GL_STATIC_DRAW);

		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, NULL);

		free(normals);
	}
	if (mesh->HasTextureCoords(0)) {
		printf("loading uv \n");
		texcoords = (float *)malloc(point_count * 2 * sizeof(float));
		for (int i = 0; i < point_count; i++) {
			const aiVector3D *vt = &(mesh->mTextureCoords[0][i]);
			texcoords[i * 2] = (float)vt->x;
			texcoords[i * 2 + 1] = (float)vt->y;
		}

		GLuint vt_vbo = 0;
		glGenBuffers(1, &vt_vbo);
		glBindBuffer(GL_ARRAY_BUFFER, vt_vbo);
		glBufferData(GL_ARRAY_BUFFER, 2 * point_count * sizeof(float), texcoords, GL_STATIC_DRAW);

		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, NULL);

		free(texcoords);
	}

	if (mesh->HasTangentsAndBitangents()) {
		printf("loading tg \n");
		vtans = (float *)malloc(point_count * 4 * sizeof(float));
		for (int i = 0; i < point_count; i++) {
			const aiVector3D *tangent = &(mesh->mTangents[i]);
			const aiVector3D *bitangent = &(mesh->mBitangents[i]);
			const aiVector3D *normal = &(mesh->mNormals[i]);

			// put the three vectors into my vec3 struct format for doing maths
			vec3 t(tangent->x, tangent->y, tangent->z);
			vec3 n(normal->x, normal->y, normal->z);
			vec3 b(bitangent->x, bitangent->y, bitangent->z);
			// orthogonalise and normalise the tangent so we can use it in something
			// approximating a T,N,B inverse matrix
			vec3 t_i = normalise(t - n * dot(n, t));

			// get determinant of T,B,N 3x3 matrix by dot*cross method
			float det = (dot(cross(n, t), b));
			if (det < 0.0f) {
				det = -1.0f;
			}
			else {
				det = 1.0f;
			}

			// push back 4d vector for inverse tangent with determinant
			vtans[i * 4] = t_i.v[0];
			vtans[i * 4 + 1] = t_i.v[1];
			vtans[i * 4 + 2] = t_i.v[2];
			vtans[i * 4 + 3] = det;

		}

		GLuint tangents_vbo = 0;
		glGenBuffers(1, &tangents_vbo);
		glBindBuffer(GL_ARRAY_BUFFER, tangents_vbo);
		glBufferData(GL_ARRAY_BUFFER, 4 * point_count * sizeof(GLfloat), vtans,
			GL_STATIC_DRAW);
		glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 0, NULL);
		glEnableVertexAttribArray(3);


		free(vtans);
	}

	return point_count;

}


#pragma endregion VBO_FUNCTIONS


void display() {

	// tell GL to only draw onto a pixel if the shape is closer to the viewer
	glEnable(GL_DEPTH_TEST); // enable depth-testing
	glDepthFunc(GL_LESS); // depth-testing interprets a smaller value as "closer"
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	//calculate interpolate position for every frame
	// manupilation of t does not work in update() ???
	t += 0.05;
	if (t >= 1.0) {
		frozeCount++;// after found goal, stop for frame
		t = 1.0;
		if (frozeCount == FROZE_TIME) {
			frozeCount = 0;
			t = 0.0;
		}
	}


	//-----------------------------------------------------
	// goal position answer keyboard event
	if (key_z_pressed) { 
		goals[0].v[0] += 0.1;
		goals[2].v[0] += 0.1;
	}
	if (key_x_pressed) { 
		goals[0].v[0] -= 0.1;
		goals[2].v[0] -= 0.1; 
	}
	if (key_c_pressed) { 
		goals[0].v[2] += 0.1;
		goals[2].v[2] += 0.1; 
	}
	if (key_v_pressed) { 
		goals[0].v[2] -= 0.1;
		goals[2].v[2] -= 0.1;
	}

	//---------------dummy arm-------------------------------
	//---------------calculate positions---------------------------------------

	skeleton kori(L1, L2, goals[0], B1, t);


	//-----------------dram body joints and arms------------------------------------
	//load_texture("../Textures/bubble.jpg", &tex);
	glEnable(GL_CULL_FACE); // cull face
	glCullFace(GL_BACK);		// cull back face
	glFrontFace(GL_CCW);		// GL_CCW for counter clock-wise

								//-------------------frame1: top left----------------------------------------
								//draw torso
	pointCount = generateObjectBufferTeapot("../Meshs/torso.obj");
	glUseProgram(shaderTx);
	//Declare your uniform variables that will be used in your shader
	int matrix_location = glGetUniformLocation(shaderTx, "model");
	int view_mat_location = glGetUniformLocation(shaderTx, "view");
	int proj_mat_location = glGetUniformLocation(shaderTx, "proj");

	mat4 view = identity_mat4();
	//look down at the scene from y axix up
	//view = translate(view, vec3(0.0, 0.0, -10.0));
	//view = look_at(vec3(-10.0, 10.0, 10.0), vec3(2.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0)); // from y+-x axis look back
	//view = look_at(vec3(-10.0, 10.0, 0.0), vec3(2.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0));
	view = look_at(vec3(0.0, 7.0, 0.0), vec3(2.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0));

	mat4 persp_proj = perspective(45.0, (float)width / (float)height, 0.01, 100.0);

	// update uniforms & draw
	glViewport(0, height / 2, width / 2, height / 2);
	glUniformMatrix4fv(proj_mat_location, 1, GL_FALSE, persp_proj.m);
	glUniformMatrix4fv(view_mat_location, 1, GL_FALSE, view.m);
	glUniformMatrix4fv(matrix_location, 1, GL_FALSE, kori.global10.m);
	glDrawArrays(GL_TRIANGLES, 0, pointCount);


	//------------draw upper arm-------------------------
	pointCount = generateObjectBufferTeapot("../Meshs/ua.obj");
	glUseProgram(shaderTx);

	// update uniforms & draw
	glUniformMatrix4fv(matrix_location, 1, GL_FALSE, kori.global1.m);
	glDrawArrays(GL_TRIANGLES, 0, pointCount);


	//-------child---lowerArm------------
	pointCount = generateObjectBufferTeapot("../Meshs/la.obj");
	glUseProgram(shaderTx);

	glUniformMatrix4fv(matrix_location, 1, GL_FALSE, kori.global3.m);
	glDrawArrays(GL_TRIANGLES, 0, pointCount);


	//-------goal---head-----------
	pointCount = generateObjectBufferTeapot("../Meshs/skull.obj");
	glUseProgram(shaderRed);

	//Declare your uniform variables that will be used in your shader
	matrix_location = glGetUniformLocation(shaderRed, "model");
	view_mat_location = glGetUniformLocation(shaderRed, "view");
	proj_mat_location = glGetUniformLocation(shaderRed, "proj");

	glUniformMatrix4fv(matrix_location, 1, GL_FALSE, kori.global0.m);
	glUniformMatrix4fv(proj_mat_location, 1, GL_FALSE, persp_proj.m);
	glUniformMatrix4fv(view_mat_location, 1, GL_FALSE, view.m);
	glDrawArrays(GL_TRIANGLES, 0, pointCount);


	//------------------------------------------------frame2: top right---------------------------------

	skeleton kori2(L1, L2, goals[1], B1, t);

	//-----------------------draw-------------------------------
	//draw torso
	pointCount = generateObjectBufferTeapot("../Meshs/torso.obj");
	glUseProgram(shaderTx);

	// update uniforms & draw
	glViewport(width / 2, height / 2, width / 2, height / 2);
	glUniformMatrix4fv(matrix_location, 1, GL_FALSE, kori2.global10.m);
	glDrawArrays(GL_TRIANGLES, 0, pointCount);


	//------------draw upper arm-------------------------
	pointCount = generateObjectBufferTeapot("../Meshs/ua.obj");
	glUseProgram(shaderTx);
	// update uniforms & draw
	glUniformMatrix4fv(matrix_location, 1, GL_FALSE, kori2.global1.m);
	glDrawArrays(GL_TRIANGLES, 0, pointCount);

	//-------child---lowerArm------------
	pointCount = generateObjectBufferTeapot("../Meshs/la.obj");
	glUseProgram(shaderTx);

	glUniformMatrix4fv(matrix_location, 1, GL_FALSE, kori2.global3.m);
	glDrawArrays(GL_TRIANGLES, 0, pointCount);


	//-------goal---head-----------
	pointCount = generateObjectBufferTeapot("../Meshs/skull.obj");
	glUseProgram(shaderRed);

	//Declare your uniform variables that will be used in your shader
	matrix_location = glGetUniformLocation(shaderRed, "model");
	view_mat_location = glGetUniformLocation(shaderRed, "view");
	proj_mat_location = glGetUniformLocation(shaderRed, "proj");

	glUniformMatrix4fv(matrix_location, 1, GL_FALSE, kori2.global0.m);
	glUniformMatrix4fv(proj_mat_location, 1, GL_FALSE, persp_proj.m);
	glUniformMatrix4fv(view_mat_location, 1, GL_FALSE, view.m);
	glDrawArrays(GL_TRIANGLES, 0, pointCount);


	//---------------------------------------------frame3: bottom left-----same to frame 1 different cam angle----------------------------

	//-----------------------draw-------------------------------
	//draw torso
	pointCount = generateObjectBufferTeapot("../Meshs/torso.obj");
	glUseProgram(shaderTx);

	// update uniforms & draw
	glViewport(0, 0, width / 2, height / 2);
	view = look_at(vec3(-5.0, 5.0, 5.0), vec3(2.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0));
	glUniformMatrix4fv(view_mat_location, 1, GL_FALSE, view.m);

	glUniformMatrix4fv(matrix_location, 1, GL_FALSE, kori.global10.m);
	glDrawArrays(GL_TRIANGLES, 0, pointCount);


	//------------draw upper arm-------------------------
	pointCount = generateObjectBufferTeapot("../Meshs/ua.obj");
	glUseProgram(shaderTx);
	// update uniforms & draw
	glUniformMatrix4fv(matrix_location, 1, GL_FALSE, kori.global1.m);
	glDrawArrays(GL_TRIANGLES, 0, pointCount);

	//-------child---lowerArm------------
	pointCount = generateObjectBufferTeapot("../Meshs/la.obj");
	glUseProgram(shaderTx);

	glUniformMatrix4fv(matrix_location, 1, GL_FALSE, kori.global3.m);
	glDrawArrays(GL_TRIANGLES, 0, pointCount);


	//-------goal---head-----------
	pointCount = generateObjectBufferTeapot("../Meshs/skull.obj");
	glUseProgram(shaderRed);

	//Declare your uniform variables that will be used in your shader
	matrix_location = glGetUniformLocation(shaderRed, "model");
	view_mat_location = glGetUniformLocation(shaderRed, "view");
	proj_mat_location = glGetUniformLocation(shaderRed, "proj");



	glUniformMatrix4fv(matrix_location, 1, GL_FALSE, kori.global0.m);
	glUniformMatrix4fv(proj_mat_location, 1, GL_FALSE, persp_proj.m);
	glUniformMatrix4fv(view_mat_location, 1, GL_FALSE, view.m);
	glDrawArrays(GL_TRIANGLES, 0, pointCount);

	//-----------------------------------frame 4- hand---CCD-------------------------------------------
	


	//---------------dummy arm-------------------------------
	max_try++;
	max_try = min(max_try, MAX_TRY); // freeze for a while / FREEZE_TIME
	th += 1.0;

	if (th == FREEZE_TIME) {
		th = 0.0;
		max_try = 1;
	}

	//--------------------------------
	//move origin/base using spline to get near to the goal/skull
	//----------------------spline----------------------------
	//cubic bezier
	//4 control point
	//now pos
	vec3 p0 = vec3(0.0, 0.0, 0.0);
	//desired pos 
	vec3 p3 = vec3(goals[2].v[0] - L0, 0.0, goals[2].v[2] - L0); // near skull but not on skull, L0 is the palm length
	//4 control point : using coordination of goal's position and 1st finger-section's length
	//control for x-z plain, toward skull postion
	vec3 p1 = vec3(L3, 0.0, L3);  //L3 is the 1st fingers length
	vec3 p2 = vec3(goals[2].v[0], 0.0, goals[2].v[2]); 
	//control for y
	float p4 = L3;
	float p5 = goals[2].v[1];
	//calculate interpolate position for every frame
	// manupilation of t does not work in update() ???
	//3D Bezier cubic:
	vec3 interP = cubicBezier3D(p0, p1, p2, p3, p4, p5, t);

	//origin move
	mat4 origin = identity_mat4();
    origin = translate(origin, interP);

	//------------------negerate hand------------------------

	Hand hand(origin, L0, L3, L4, L5, goals[2], max_try);

	//---------------------------------------
	//-------child---1st finger------------
	pointCount = generateObjectBufferTeapot("../Meshs/finger1.obj");
	glUseProgram(shaderTx);

	//Declare your uniform variables that will be used in your shader
	matrix_location = glGetUniformLocation(shaderTx, "model");
	view_mat_location = glGetUniformLocation(shaderTx, "view");


	view = look_at(vec3(0.0, 10.0, 0.0), vec3(2.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0)); // from y axis look back


																					// update uniforms & draw
	glViewport(width / 2, 0, width / 2, height / 2);
	glUniformMatrix4fv(view_mat_location, 1, GL_FALSE, view.m);
	glUniformMatrix4fv(matrix_location, 1, GL_FALSE, hand.global2.m);
	glDrawArrays(GL_TRIANGLES, 0, pointCount);


	//-------child---2nd finger------------
	pointCount = generateObjectBufferTeapot("../Meshs/finger2.obj");
	glUseProgram(shaderTx);

	//Declare your uniform variables that will be used in your shader
	matrix_location = glGetUniformLocation(shaderTx, "model");

	glUniformMatrix4fv(matrix_location, 1, GL_FALSE, hand.global4.m);
	glDrawArrays(GL_TRIANGLES, 0, pointCount);

	//-------child---3rd finger------------
	pointCount = generateObjectBufferTeapot("../Meshs/finger3.obj");
	glUseProgram(shaderTx);

	//Declare your uniform variables that will be used in your shader
	matrix_location = glGetUniformLocation(shaderTx, "model");

	glUniformMatrix4fv(matrix_location, 1, GL_FALSE, hand.global6.m);
	glDrawArrays(GL_TRIANGLES, 0, pointCount);

	//-------base palm------------
	pointCount = generateObjectBufferTeapot("../Meshs/palm.obj");
	glUseProgram(shaderTx);

	//Declare your uniform variables that will be used in your shader
	matrix_location = glGetUniformLocation(shaderTx, "model");

	glUniformMatrix4fv(matrix_location, 1, GL_FALSE, hand.global10.m);
	glDrawArrays(GL_TRIANGLES, 0, pointCount);



	//-------goal---skull-----------
	pointCount = generateObjectBufferTeapot("../Meshs/skull.obj");
	glUseProgram(shaderRed);

	//Declare your uniform variables that will be used in your shader
	matrix_location = glGetUniformLocation(shaderRed, "model");
	view_mat_location = glGetUniformLocation(shaderRed, "view");

	glUniformMatrix4fv(matrix_location, 1, GL_FALSE, hand.global0.m);
	glUniformMatrix4fv(view_mat_location, 1, GL_FALSE, view.m);
	glDrawArrays(GL_TRIANGLES, 0, pointCount);




	//----------------------------------------

	//swap buffer for all 
	glutSwapBuffers();
}



void updateScene() {

	// Placeholder code, if you want to work with framerate
	// Wait until at least 16ms passed since start of last frame (Effectively caps framerate at ~60fps)
	static DWORD  last_time = 0;
	DWORD  curr_time = timeGetTime();
	float  delta = (curr_time - last_time) * 0.001f;
	if (delta > 0.03f)
		delta = 0.03f;
	last_time = curr_time;

	// 0.2f as unit, rotate every frame
	rotatez += .5f;

	// Draw the next frame
	glutPostRedisplay();
}


void init()
{
	// Set up the shaders
	shaderTx = CompileShaders(shaderTx, "../Shaders/PhongBVSGrey.glsl", "../Shaders/PhongBFSGrey.glsl");
	shaderRed = CompileShaders(shaderRed, "../Shaders/ToonVS.glsl", "../Shaders/ToonFS.glsl");

	//add in textures
	//load_texture("../Textures/brick.jpg", &tex);


}

#pragma region KEYBOARD
// Placeholder code for the keypress
void keypress(unsigned char key, int x, int y) {

	if (key == 'x') {
		//Translate the base, etc.
		key_x_pressed = true;
		display();
	}

	if (key == 'z') {
		//Translate the base, etc.
		key_z_pressed = true;
		display();
	}

	if (key == 'c') {
		//Rotate the base, etc.
		key_c_pressed = true;
		display();
	}

	if (key == 'v') {
		//Rotate the base, etc.
		key_v_pressed = true;
		display();
	}

	if (key == 'b') {
		//Scale the base, etc.
		key_b_pressed = true;
		display();
	}

	if (key == 'n') {
		//Scale the base, etc.
		key_n_pressed = true;
		display();
	}



	if (key == 's') {
		//Rotate the base, etc.
		key_s_pressed = true;
		display();
	}

	if (key == 'a') {
		//Rotate the base, etc.
		key_a_pressed = true;
		display();
	}

	if (key == 'h') {
		//Rotate the base, etc.
		key_h_pressed = true;
		display();
	}

	if (key == 'd') {
		//Rotate the base, etc.
		key_d_pressed = true;
		display();
	}

	if (key == 'f') {
		//Scale the base, etc.
		key_f_pressed = true;
		display();
	}

	if (key == 'g') {
		//Scale the base, etc.
		key_g_pressed = true;
		display();
	}



	if (key == 'w') {
		//Scale the base, etc.
		key_w_pressed = true;
		display();
	}

	if (key == 'q') {
		//Scale the base, etc.
		key_q_pressed = true;
		display();
	}

}

void keyUp(unsigned char key, int x, int y) {
	if (key == 'x') {
		key_x_pressed = false;
		display();
	}

	if (key == 'z') {
		//Translate the base, etc.
		key_z_pressed = false;
		display();
	}

	if (key == 'c') {
		key_c_pressed = false;
		display();
	}

	if (key == 'v') {
		//Translate the base, etc.
		key_v_pressed = false;
		display();
	}

	if (key == 'b') {
		//Rotate the base, etc.
		key_b_pressed = false;
		display();
	}

	if (key == 'n') {
		//Rotate the base, etc.
		key_n_pressed = false;
		display();
	}




	if (key == 's') {
		//Rotate the base, etc.
		key_s_pressed = false;
		display();
	}

	if (key == 'a') {
		//Rotate the base, etc.
		key_a_pressed = false;
		display();
	}

	if (key == 'd') {
		//Rotate the base, etc.
		key_d_pressed = false;
		display();
	}

	if (key == 'f') {
		//Rotate the base, etc.
		key_f_pressed = false;
		display();
	}


	if (key == 'g') {
		//Rotate the base, etc.
		key_g_pressed = false;
		display();
	}

	if (key == 'h') {
		//Rotate the base, etc.
		key_h_pressed = false;
		display();
	}




	if (key == 'w') {
		//Scale the base, etc.
		key_w_pressed = false;
		display();
	}

	if (key == 'q') {
		//Scale the base, etc.
		key_q_pressed = false;
		display();
	}
}
#pragma endregion KEYBOARD

#pragma region MOUSE EVENT
void mouseClick(int button, int state, int x, int y) {
	if (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN) {



	}
}
#pragma region MOUSE EVENT


int main(int argc, char** argv) {

	// Set up the window
	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
	glutInitWindowSize(width, height);
	glutCreateWindow("Transform");

	// Tell glut where the display function is
	glutDisplayFunc(display);
	glutIdleFunc(updateScene);
	// glut keyboard callbacks
	glutKeyboardFunc(keypress);
	glutKeyboardUpFunc(keyUp);
	// glut mouse callbacks
	glutMouseFunc(mouseClick); //check for mouse

							   // A call to glewInit() must be done after glut is initialized!
	GLenum res = glewInit();
	// Check for any errors
	if (res != GLEW_OK) {
		fprintf(stderr, "Error: '%s'\n", glewGetErrorString(res));
		return 1;
	}
	// Set up your objects and shaders
	init();

	// Begin infinite event loop
	glutMainLoop();
	return 0;
}











